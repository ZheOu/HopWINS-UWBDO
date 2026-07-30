# MCU Build

The MCU project produces one firmware image. Physical PCB population and the
application workflow are selected together in the `USER CODE BEGIN PD` block
of `Src/main.c`:

```c
#define HOPWINS_PCB_PROFILE       BOARD_PROFILE_FOLLOWER_FULL
#define HOPWINS_INSTALLED_XO      BOARD_CLOCK_XO_I2C
#define HOPWINS_APP_WORKFLOW      APP_WORKFLOW_DO_FOLLOWER
```

Use `BOARD_PROFILE_UWB_ONLY` with `BOARD_CLOCK_XO_NONE` for the UWB-only PCB.
The full PCB accepts `BOARD_CLOCK_XO_I2C`, `BOARD_CLOCK_XO_CLKDP`, or
`BOARD_CLOCK_XO_NONE` to describe the fitted oscillator. The follower workflow
requires FPGA, clock control, and the TIM2 external reference counter; startup
rejects incompatible selections.

The same block owns temporary boot diagnostics and CIR capture size. A CIR
sample count of zero exports the complete accumulator.

## Build

The project uses the STM32Cube ST Arm Clang bundle and Ninja. CMake has only two
presets:

| Preset | Output |
|---|---|
| `Debug` | `build/Debug/HopWINS-UWBDO-MCU.elf` |
| `Release` | `build/Release/HopWINS-UWBDO-MCU.elf` |

The FPGA bitstream is always embedded in the linker-reserved 128 KiB region at
`0x081E0000`. Keeping one ELF avoids a board/role/build matrix and lets ST-Link
program the MCU application and FPGA image together.

The Cube bundle lock may still list GNU tools because ST-Link debugging uses
GNU GDB. They are not used as the C or assembler compiler; both build presets
resolve `starm-clang`.

`MCU/.clangd` normalizes the compiler name and removes Clang's private
`newlib.cfg` option from clangd's view of each command. CMake exports the
equivalent target and system include paths explicitly, avoiding
version-sensitive compiler probing while preserving the real compiler's
Newlib configuration.

## VS Code

Open the repository root, not `MCU` as a second workspace folder. The root
settings point CMake Tools at `MCU`, use Cube's CMake and Clang bundles, and
copy the selected compile database to `MCU/compile_commands.json` for clangd.

`MCU: Debug` is the only MCU launch entry. It runs the `MCU: Build Debug` task
and loads `build/Debug/HopWINS-UWBDO-MCU.elf`. The task file also exposes
explicit Debug/Release configure and build commands.
