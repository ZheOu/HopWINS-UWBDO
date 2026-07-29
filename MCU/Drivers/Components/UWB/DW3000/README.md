# DW3000 Driver Integration

This directory contains the Qorvo DW3000-family core plus the HopWINS
project-facing adapter.

## Compiled files

- `dw3000.c/.h`: Stable project API, SDK callback adapter, reset/probe/init,
  project radio configuration, TX/RX polling, frame/timestamp extraction,
  full 48-bit CIR capture, CIA diagnostics, and device-ID validation.
- `dw3000_device.c`: Qorvo register-level device implementation. Keep this
  vendor core intact unless applying a reviewed SDK fix.
- `deca_compat.c`: Qorvo public `dwt_*` compatibility API and driver probing.
- `deca_interface.c/.h`: Common TX/RX operation dispatch used by the driver
  operation table.
- `deca_rsl.c/.h`: RSSI and first-path power calculations. The imported
  `qmath.h` dependency was replaced with a local fixed-point `log2` routine.
- `deca_device_api.h`: Qorvo public types, constants, and API declarations.
- `deca_types.h`: Standard C fixed-width type includes. Legacy STM32F4
  typedef fallbacks are intentionally removed.
- `deca_private.h`, `deca_version.h`, `dw3000_deca_regs.h`, and
  `dw3000_deca_vals.h`: Internal declarations, version metadata, register
  definitions, and device constants required by the core.

## Platform integration

Legacy Qorvo example-board files (`deca_spi`, `deca_sleep`, `deca_mutex`, and
`deca_probe_interface`) are intentionally not kept in this project. Their SPI,
delay, reset, probe, and locking hooks are provided by `dw3000.c` through
`dw3000_platform_t`, with the STM32 implementation owned by `board.c`.

The current TX service polls `TXFRS` at a 1 ms rate and does not enable the DW
IRQ. Add board lock/unlock callbacks when IRQ handling is introduced.

## Bring-up flow

1. Configure SPI1 to 6 MHz, below the 7 MHz INIT_RC limit.
2. Pull RSTn low with an open-drain output, release it, and wait 2 ms.
3. Read register `0x00:0x00` directly and validate the raw device ID.
4. Probe the Qorvo DW3000 driver and verify IDLE_RC.
5. Run `dwt_initialise(DWT_READ_OTP_ALL)` to load OTP trims.
6. Switch SPI1 to 12 MHz and read the device ID again.
7. Translate the project `dw3000_radio_config_t` into Qorvo `dwt_config_t` and
   call `dwt_configure()`, `dwt_configuretxrf()`, and the antenna-delay APIs.
8. For TX, write a frame, program `DX_TIME`, issue delayed TX, poll `TXFRS`,
   and read the 40-bit transmitted timestamp.
9. For RX/CIR, enable full CIA logging before RX, wait for `RXFCG`, read the
   frame and 40-bit timestamp, then read Ipatov diagnostics and accumulator
   samples before re-enabling RX.

The board implementation owns STM32 HAL handles, pins, SPI rates, reset
electrical behavior, and delay timing. The component driver does not include
STM32 headers.
