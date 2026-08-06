# DW3220TR13 Driver Integration

This directory contains the Qorvo DW3000-family core required by DW3220TR13,
plus a small project-facing driver. It has no STM32, HAL, CubeMX, UART, or PCB
pin dependency.

## Ownership boundary

Project-maintained files:

- `dw3000.c/.h`: Stable application-facing device API and radio state.
- `dw3000_qorvo_adapter.c/.h`: Internal translation from
  `dw3000_platform_t` to Qorvo SPI, probe, delay, and lock hooks.
- `README.md`: Integration and local-patch record.

Qorvo-derived files:

- `dw3000_device.c`, `deca_compat.c`, `deca_interface.c/.h`,
  `deca_device_api.h`, `deca_private.h`, `deca_types.h`, `deca_version.h`, and
  `dw3000_deca_*.h`.
- `deca_rsl.c/.h` is Qorvo-derived, with its unavailable `qmath.h` logarithm
  replaced by the local fixed-point implementation.

Do not merge the vendor core into `dw3000.c` or reformat it wholesale. Keeping
the ownership boundary visible makes SDK updates and reviewed bug fixes
diffable. The Qorvo compatibility and MCPS files expose more features than the
current polling service uses, but the driver descriptor and central `ioctl`
dispatch reference them. Aggressively pruning those tables could save Flash,
but would create a private fork of calibration, sleep, interrupt, and future
TWR behavior for little practical benefit on the STM32U585.

## Compiled files

- `dw3000.c/.h`: Stable project API, reset/probe/init,
  project radio configuration, TX/RX polling, frame/timestamp extraction,
  full 48-bit CIR capture, CIA diagnostics, and device-ID validation.
- `dw3000_qorvo_adapter.c/.h`: Qorvo SDK callback, probe, delay, and mutex
  adaptation. This is the only project-maintained file that implements legacy
  `deca_*` platform hooks.
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
`deca_probe_interface`) are intentionally not kept. Their SPI, delay, probe,
and locking hooks are provided by `dw3000_qorvo_adapter.c` through
`dw3000_platform_t`; reset stays in the project driver. The STM32
implementation is owned by `board.c`.

SysTick scheduling and the TIM2 reference timestamp are UWB Service concerns,
not DW3220 hardware callbacks. They are injected separately through
`uwb_service_time_source_t`.

The current TX service polls `TXFRS` at a 1 ms rate and does not enable the DW
IRQ. Add board lock/unlock callbacks when IRQ handling is introduced.

## Reviewed local core patches

Keep local changes to Qorvo-derived files small and documented:

- Clang omits GCC's function-level `optimize("O3")` attribute and relies on
  the selected CMake build optimization.
- The SAR conversion poll has a finite bound so a missing reference clock
  cannot trap the MCU forever during initialization or PLL calibration.
- Invalid internal SPI transfer modes use the configured assertion path rather
  than an unconditional infinite loop.
- The RSSI preamble-code extraction explicitly parenthesizes mask-before-shift.

## Bring-up flow

1. Configure SPI1 to 6 MHz, below the 7 MHz INIT_RC limit.
2. Pull RSTn low with an open-drain output, release it, and wait 2 ms.
3. Read register `0x00:0x00` directly and validate the raw device ID.
4. Probe the Qorvo DW3000 driver and verify IDLE_RC.
5. Run `dwt_initialise(DWT_READ_OTP_ALL)` to load OTP trims.
6. Switch SPI1 to 12 MHz and read the device ID again.
7. Translate the project `dw3000_radio_config_t` into Qorvo `dwt_config_t`,
   call `dwt_configure()`, apply the selected manual/automatic RF mode, then
   configure TX RF and calibrated antenna delays.
8. For TX, write a frame, program `DX_TIME`, issue delayed TX, poll `TXFRS`,
   and read the 40-bit transmitted timestamp.
9. For RX/CIR, enable full CIA logging before RX, wait for `RXFCG`, read the
   frame and 40-bit timestamp, snapshot important RX/CIA registers, then read
   Ipatov diagnostics and accumulator samples before re-enabling RX.

The board implementation owns STM32 HAL handles, pins, SPI rates, reset
electrical behavior, and delay timing. The component driver does not include
STM32 headers.
