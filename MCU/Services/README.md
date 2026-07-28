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

The startup DW3000 clock test is controlled by the
`HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC` CMake option. The `Debug` preset
enables it explicitly; release-oriented presets leave it disabled. Future
interactive diagnostics should call `fpga_service_*()` and
`uwb_service_*()` from command handlers instead of accessing component drivers
directly.
