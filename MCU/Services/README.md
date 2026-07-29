# MCU Services

Services own application workflows and state for the single physical devices on
the HopWINS board. They sit between the generated application entry point and
the board/component layers.

## Dependency direction

```text
main.c -> App -> selected role service
role service -> device services / Console
device services -> component drivers
role service / Console -> Board
component drivers -> platform callbacks
Board -> STM32 HAL
```

Keep these boundaries when extending the firmware:

- `main.c` only initializes CubeMX peripherals, the board, and `App`.
- `App` calls the one role implementation selected by CMake; it contains no
  board or radio-mode policy.
- `Roles` own the Leader/Follower workflows and future transitions between DO
  operation and TWR calibration.
- Component drivers contain chip protocol and configuration timing, but no HAL,
  board pin names, UART reporting, or application policy.
- `Board` owns concrete pins, STM32 handles, and HAL-backed platform callbacks.
- Device services own state, retry policy, and operations that future console
  commands or application modes can invoke.
- `Console` owns UART presentation, framing, and CRC. Device services return
  structured state and never print directly.

## Periodic UWB TX

`UWB/uwb_profile.c` is the application-owned configuration point for the
current channel, preamble, data rate, TX RF settings, antenna delays, and PAN
addresses. `UWB_PERIODIC_TX_INTERVAL_US` in `uwb_service.c` owns the periodic
test interval. The service serializes the test frame and maintains the
delayed-TX state machine; Qorvo `dwt_*` calls remain private to the DW3000
component adapter.

The default profile transmits once per second on channel 5. It is a bench
bring-up profile: TX power and antenna delays must be replaced with calibrated
values for the final PCB and deployment region.

Each test frame contains:

```text
IEEE 802.15.4 header | "HWDO" | sequence (LE32) | scheduled time (LE32) | FCS
```

The DW3000 appends the two-byte FCS. The service schedules each frame from the
previous target time so software latency does not accumulate into the radio
interval.

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

The startup DW3000 clock test is independently controlled by the
`HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC` CMake option and is disabled by
default for every product preset. Future interactive diagnostics should call
`fpga_service_*()` and
`uwb_service_*()` from command handlers instead of accessing component drivers
directly.

The `DO_LEADER` role owns periodic transmission. Radio tasks are selected by
the role state machine rather than independent CMake TX/RX switches, allowing
the same firmware to pause DO and run occasional TWR calibration later.

## UWB RX and CIR export

The `DO_FOLLOWER` role continuously receives UWB frames and captures CIR.
After a CRC-correct frame, `uwb_service.c` records MCU SysTick, the external
TIM2 reference counter, the DW3000 RX and system timestamps, RF port,
`SYS_STATUS`, `RX_FINFO`, CIA status registers, carrier/clock offsets, Ipatov
diagnostics, and the accumulator before re-enabling RX. The radio is forced to
RF Port 1 for all current TX and RX operations. A valid frame is still exported
when optional register, diagnostic, or CIR reads fail; their signed status
fields identify which metadata is unavailable.

`HOPWINS_UWB_CIR_SAMPLE_COUNT` controls the capture size. Zero exports the
complete accumulator (1016 samples for the default PRF64 code). A smaller value
captures a window around the detected first path;
`HOPWINS_UWB_CIR_PRE_FIRST_PATH_SAMPLES` controls how many samples precede it.

Console exports one frame packet followed by CRC-protected CIR chunks through
the existing USART1 DMA queue. See
[`Console/CIR_PROTOCOL.md`](Console/CIR_PROTOCOL.md) for the binary layout,
CRC parameters, and throughput budget.

RX uses two frame/CIR capture slots. After the driver has copied one frame,
register snapshot, diagnostics, and accumulator data, it immediately re-arms
the DW3000 into the other slot while Console exports the first. RX pauses only
when both slots are queued, which is reported by the `QFULL` health counter.
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

## Product configuration

The three independent build dimensions are:

```text
HOPWINS_BOARD_TARGET = LEADER_UWB_ONLY | FOLLOWER_FULL
HOPWINS_APP_ROLE     = DO_LEADER | DO_FOLLOWER
CMAKE_BUILD_TYPE     = Debug | Release
```

`Boards/HopWINS-UWBDO-PCB-STDWDO/board_config.h` maps the selected physical
target to compile-time FPGA, clock-control, external-counter, and installed-XO
capabilities. Its adjacent `board_target.cmake` owns the corresponding CMake
selection. Roles do not define hardware population. CMake rejects a
`DO_FOLLOWER` role on hardware without FPGA and clock control.

The supported product presets are `Leader-Debug`, `Leader-Release`,
`Follower-Debug`, and `Follower-Release`. Leader builds omit the FPGA driver
and image. Follower builds embed the image and verify that CDONE goes Low while
reset is asserted, so an absent FPGA cannot be mistaken for configuration
success.
