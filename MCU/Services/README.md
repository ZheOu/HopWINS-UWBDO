# MCU Services

Services own application workflows and state for the single physical devices on
the HopWINS board. They sit between the generated application entry point and
the board/component layers.

## Dependency direction

```text
main.c -> App -> device services / Console
device services -> component drivers
App / Console -> Board
component drivers -> platform callbacks
Board -> STM32 HAL
```

Keep these boundaries when extending the firmware:

- `main.c` only initializes CubeMX peripherals, the board, and `App`.
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

The startup DW3000 clock test is controlled by the
`HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC` CMake option. The `Debug` preset
enables it explicitly; release-oriented presets leave it disabled. Future
interactive diagnostics should call `fpga_service_*()` and
`uwb_service_*()` from command handlers instead of accessing component drivers
directly.

Periodic test transmission is controlled by the
`HOPWINS_ENABLE_PERIODIC_UWB_TX` CMake option and is enabled by default.
