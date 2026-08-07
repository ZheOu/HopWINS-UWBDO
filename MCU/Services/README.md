# MCU Services

Services own application workflows and state for the single physical devices on
the HopWINS board. They sit between the generated application entry point and
the board/component layers.

## Dependency direction

```text
main.c -> App -> runtime workflow dispatcher
Workflows -> Algorithms / Devices / Communication
Algorithms -> structured capture and timing data only
Devices -> device drivers / Board platform callbacks
Communication -> Board UART and CRC services
device drivers -> platform callbacks
Board -> STM32 HAL
```

Keep these boundaries when extending the firmware:

- `main.c` only initializes CubeMX peripherals, the board, and `App`.
- `App` starts the selected workflow and runs the top-level process functions.
- `Workflows` own Leader/Follower sequencing, recovery, and composition. They
  may coordinate several devices but contain no chip-register access.
- `Algorithms` are replaceable, HAL-free timing and control policies. A new
  timestamp estimator or DO loop is registered here, not copied into a workflow.
- `Devices` own one physical device's state and reusable operations. They do
  not decide which experiment or serial format is active.
- `Communication/Serial` owns UART presentation, framing, protocol versions, and
  CRC. Device services return structured state and never print directly.
- `Diagnostics` is reserved for reusable bring-up procedures. Temporary tests
  must be explicitly selected and must not be hidden in a normal workflow.
- Device drivers contain chip protocol and configuration timing, but no HAL,
  board pin names, UART reporting, or application policy.
- `Board` owns concrete pins, STM32 handles, and HAL-backed platform callbacks.

## Periodic UWB TX

`Devices/UWB/uwb_profile.c` is the application-owned configuration point for the
current channel, preamble, data rate, TX RF settings, antenna delays, and PAN
addresses. `UWB_PERIODIC_TX_INTERVAL_US` in `uwb_service.c` owns the 100 ms
test interval. The service serializes the test frame and maintains the
delayed-TX state machine; Qorvo `dwt_*` calls remain private to the DW3000
device adapter.

The default profile transmits at 10 Hz on channel 5. It is a bench
bring-up profile: TX power and antenna delays must be replaced with calibrated
values for the final PCB and deployment region.

Each test frame contains:

```text
IEEE 802.15.4 header | "HWDO" | sequence (LE32) | scheduled time (LE32) | FCS
```

The DW3000 appends the two-byte FCS. The service schedules each frame from the
previous target time so software latency does not accumulate into the radio
interval.

## STS/PDoA diagnostic

`Diagnostics/UWB/uwb_sts_diagnostic_service.c` provides explicit TX and dual-RX
workflows. Both use STS Mode 1 with SDC and 256 symbols. TX sends from RF1 at
the normal 100 ms interval; RX uses PDoA Mode 3 with automatic RF1-to-RF2
switching, waits for `CIADONE`, and reads the complete STS0 and STS1
accumulators before re-enabling reception. It does not configure the FPGA or
run clock discipline. The workflow dispatcher applies this fixed diagnostic
radio profile so ordinary Leader/Follower profiles remain unchanged.

## PC UART transport

USART1 runs at 5 Mbps with its hardware FIFO enabled and GPDMA1 Channel 0
draining a 16 KiB board-owned TX ring. `board_pc_transmit()` copies into this
queue and returns immediately; `BOARD_BUSY` reports backpressure when capacity
is unavailable. This makes stack-backed Console messages safe and provides the
transport base for future CIR export.

CIR samples should use a binary, length-delimited protocol written in chunks
after checking `board_pc_tx_available()`. Do not encode CIR arrays as diagnostic
hex text: that multiplies bandwidth and CPU cost. The current CRC-suffixed text
messages remain intended for low-rate status and TX event reporting.

Future interactive diagnostics should call `fpga_service_*()` and
`uwb_service_*()` instead of accessing device drivers directly, and should
live under `Diagnostics` when they are useful beyond one temporary bench test.

The FPGA path follows the same ownership model as the clock drivers.
`ice40up5k.c` owns only iCE40UP5K image checks and one SPI configuration
sequence through platform callbacks. `Devices/FPGA/fpga_image.c` owns
linker-symbol access, while `Devices/FPGA/fpga_service.c` owns retained state
and diagnostics. The
Board implementation owns the shared SPI1 details, HAL-sized transfer chunks,
CRESET_B/CDONE pins, and the PCB's separate `FPGA_EN` user-logic signal. The
Follower keeps `FPGA_EN` low until configuration succeeds, then enables the
current RTL explicitly.

The `APP_WORKFLOW_DO_LEADER` workflow owns periodic transmission. Radio tasks
are selected by the workflow dispatcher rather than independent CMake TX/RX switches, allowing
the same firmware to pause DO and run occasional TWR calibration later.

## UWB RX, clock tracking, and CIR export

The `APP_WORKFLOW_DO_FOLLOWER` workflow continuously receives UWB frames. CIR
capture is enabled when either the selected timestamp estimator or serial
capture format requires it.
After a CRC-correct frame, `uwb_service.c` records MCU SysTick, the external
TIM2 reference counter, the DW3000 RX and system timestamps, RF port,
`SYS_STATUS`, `RX_FINFO`, CIA status registers, carrier/clock offsets, Ipatov
diagnostics, and the accumulator before re-enabling RX. RF path selection comes
from `HOPWINS_UWB_RF_MODE`. A valid frame is still exported
when optional register, diagnostic, or CIR reads fail; their signed status
fields identify which metadata is unavailable.

`HOPWINS_CIR_SAMPLE_COUNT` in `main.c` controls the capture size. Zero exports the
complete accumulator (1016 samples for the default PRF64 code). A smaller value
captures a window around the detected first path;
`HOPWINS_CIR_PRE_FIRST_PATH_SAMPLES` controls how many samples precede it.

`SERIAL_CAPTURE_FORMAT_HCIR_V2` preserves the original single-CIR wire format.
`SERIAL_CAPTURE_FORMAT_HCIR_V3` adds CIR source and PDoA/STS metadata. See
[`Communication/Serial/HCIR_PROTOCOL.md`](Communication/Serial/HCIR_PROTOCOL.md)
for both layouts, CRC parameters, and throughput budgets.

`SERIAL_CAPTURE_FORMAT_TEXT_V1` emits one CRC-protected, human-readable metadata
record per received frame without CIR samples. `SERIAL_CAPTURE_FORMAT_OFF` is
the low-overhead clock-tracking mode. HCIR v2 remains byte-for-byte compatible
with existing recordings, while HCIR v3 identifies paired STS accumulators by
one shared capture ID and separate CIR-source fields.

RX uses four frame/CIR capture slots. After the driver has copied one frame,
register snapshot, diagnostics, and accumulator data, it immediately re-arms
the DW3000 when enough slots remain for the selected capture group. A normal
Ipatov capture consumes one slot; a Mode-3 STS0/STS1 pair consumes two. RX
pauses only when the next complete group cannot fit, which is reported by the
`QFULL` health counter.
This removes the full UART wire time from the normal receive blind interval.

The Follower retries failed UWB initialization once per second. During normal
operation, RX errors are re-armed with a short backoff; a five-second no-frame
watchdog restarts RX, and three consecutive watchdog restarts request a full
UWB reinitialization. `UWB RX HEALTH` reports receive, CRC-error, recovery,
watchdog, queue-full, and UART-DMA error counters every five seconds.

UART DMA has an independent 100 ms progress timeout. A timed-out transfer is
aborted and retried from its ring-buffer tail. The host HCIR parser discards
any partial duplicate prefix by length and CRC before resynchronizing at the
next valid magic.

## Runtime configuration

Physical population and workflow are selected in `Src/main.c`:

```c
#define HOPWINS_BOARD_VARIANT  BOARD_VARIANT_UWB_RF1_SIT5156
#define HOPWINS_APP_WORKFLOW   APP_WORKFLOW_DO_FOLLOWER
#define HOPWINS_UWB_RF_MODE    DW3000_RF_MODE_MANUAL_1
#define HOPWINS_UWB_PDOA_MODE  DW3000_PDOA_MODE_DISABLED
#define HOPWINS_DO_LOOP_STRATEGY DO_LOOP_STRATEGY_ENDPOINT_SLOPE
#define HOPWINS_TIMESTAMP_ESTIMATOR UWB_TIMESTAMP_ESTIMATOR_DW_ADJUSTED
#define HOPWINS_FOLLOWER_CAPTURE_FORMAT SERIAL_CAPTURE_FORMAT_OFF
```

`board_init()` loads one exact BOM variant. The description contains only
physical facts: fitted clock device, available RF paths, FPGA population, and
the TIM2 reference connection. The oscillator no longer has a separate setting
that can conflict with the Board variant.

RF selection is application policy. The workflow copies the default UWB
profile, applies `HOPWINS_UWB_RF_MODE`, and verifies that the selected Board has
all required paths. Manual modes need one path and disabled PDoA; `AUTO_1_2`
and `AUTO_2_1` need both paths plus PDoA mode 1 or 3. The Follower skips FPGA
initialization when the Board description reports that the FPGA is absent.
CMake only selects Debug or Release; both builds contain all workflows,
device drivers, and the FPGA image.

Algorithm and output choices are ordinary `app_config_t` values, not build
profiles. `UWB_TIMESTAMP_ESTIMATOR_DW_ADJUSTED` uses the DW3000 corrected RX
timestamp; `UWB_TIMESTAMP_ESTIMATOR_DW_RAW` uses the unadjusted 40-bit RX
timestamp. `DO_LOOP_STRATEGY_ENDPOINT_SLOPE` preserves the original endpoint
calculation. `DO_LOOP_STRATEGY_WEIGHTED_BASELINES` combines all valid baseline
estimates from interval four through the selected window with interval-squared
weights, a fixed-point approximation of a zero-intercept slope fit. Both feed
the same clock actuator and safety limits.

For a SiT5156 Board variant, the clock service addresses the fitted
`SiT5156AC-FA025JH-38.400000` at 7-bit address `0x60`. This J-option uses
software output enable, so boot restores the 26-bit pull word to zero, writes
`0x01[10]=1`, and reads registers `0x00` through `0x02` back. It then waits the
datasheet's 45 ms maximum time to rated stability and confirms that the
38.4 MHz path advances TIM2 through its 38399 prescaler. `CLOCK INIT OK`
therefore means both I2C control and a physical output clock were observed.

The current PCB schematic shows a 10 kOhm pull-up on SDA but no matching
external pull-up on SCL, while the STM32 I2C pins are configured with no
internal pull. Fit an SCL pull-up to `CLK_2V5` before treating 400 kHz I2C as a
compliant interface; do not use the STM32's 3.3 V internal pull-up on this
2.5 V oscillator bus.

For controlled RF-path bring-up, `HOPWINS_FOLLOWER_RF_AB_TEST` in `main.c`
alternates the Follower between RF1 and RF2. The interval is selected by
`HOPWINS_FOLLOWER_RF_AB_INTERVAL_MS`; switching waits until the current CIR
capture has finished exporting, stops RX, changes the manual RF port, and
restarts RX. Every manual-mode HCIR capture records the active RF port; the
legacy field is zero in automatic dual-path mode. Disable this temporary test
after the RF-path comparison.

## Ideal-channel DO tracking experiment

The Leader sends DW3000 delayed transmissions at 100 ms intervals. Select its
physical Board variant independently from `APP_WORKFLOW_DO_LEADER`; select the
connected RF cable with `HOPWINS_UWB_RF_MODE`.

The current Follower experiment selects `BOARD_VARIANT_UWB_RF1_SIT5156`,
`DW3000_RF_MODE_MANUAL_1`, and `APP_WORKFLOW_DO_FOLLOWER`. This uses RF1 and
the SiT5156 while skipping FPGA configuration. `HOPWINS_DO_CLOCK_TRACKING=true`
enables the timestamp-slope loop, while
`HOPWINS_FOLLOWER_CAPTURE_FORMAT=SERIAL_CAPTURE_FORMAT_OFF` selects the
low-overhead timing-only RX
path. RF A/B switching is rejected while tracking because changing paths also
changes the fixed propagation delay.

Each Leader frame carries its sequence and delayed-TX register time. The
Follower converts the delayed-TX time to the 40-bit timestamp scale and, over
the configured number of packet intervals, estimates
`(follower_delta - leader_delta) / leader_delta` in ppb. Fixed cable and
attenuator delay cancels in this difference. A positive estimate means the
Follower runs fast, so the absolute oscillator pull command is reduced by the
measured residual. With the default endpoint strategy, the 20-interval window
is the only averaging. The multi-baseline strategy is available for a direct
performance comparison without changing the workflow.

`DO TRACK` UART records report the two sequence numbers, both elapsed DTU
values, measured error, absolute pull command, strategy observation count,
update count, and reject count.
Every text record retains the existing CRC32 suffix. Windows beyond 200 ppm are
rejected. The clock service supplies the command limit for the selected part:
the present SiT5156 build uses +/-200 ppm, while the SiT3907 path uses a
conservative +/-1500 ppm inside its +/-1600 ppm range.
