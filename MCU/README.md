# MCU Build

The MCU project produces one firmware image. Physical PCB population and the
application workflow are selected together in the `USER CODE BEGIN PD` block
of `Src/main.c`:

```c
#define HOPWINS_BOARD_VARIANT  BOARD_VARIANT_UWB_RF1_SIT5156
#define HOPWINS_APP_WORKFLOW   APP_WORKFLOW_DO_FOLLOWER
#define HOPWINS_UWB_RF_MODE    DW3000_RF_MODE_MANUAL_1
#define HOPWINS_UWB_PDOA_MODE  DW3000_PDOA_MODE_DISABLED
```

Each Board variant is one real BOM population: `BOARD_VARIANT_UWB_RF1`,
`BOARD_VARIANT_UWB_RF1_SIT5156`, `BOARD_VARIANT_FULL_SIT5156`, or
`BOARD_VARIANT_FULL_SIT3907`. The oscillator is part of that physical variant,
so it cannot conflict with a second runtime setting. The Follower workflow
requires a clock device and the TIM2 counter; FPGA configuration is performed
only when the selected variant declares an FPGA fitted.

The RF mode is application policy. Manual modes require the corresponding RF
path and disabled PDoA. `DW3000_RF_MODE_AUTO_1_2` and `AUTO_2_1` require both
paths plus PDoA mode 1 or 3. Startup rejects an inconsistent mode or one that
the selected Board variant cannot physically support.

The same block owns the DO tracking experiment and CIR capture settings. A CIR
sample count of zero exports the complete accumulator when CIR export is
enabled.

The STS/PDoA bench diagnostic is selected with one of these pairs:

```c
/* Single-RF TX board. */
#define HOPWINS_BOARD_VARIANT  BOARD_VARIANT_UWB_RF1_SIT5156
#define HOPWINS_APP_WORKFLOW   APP_WORKFLOW_UWB_STS_TX_DIAGNOSTIC

/* Dual-RF RX board; this is the current checked-in selection. */
#define HOPWINS_BOARD_VARIANT  BOARD_VARIANT_FULL_SIT5156
#define HOPWINS_APP_WORKFLOW   APP_WORKFLOW_UWB_STS_DUAL_RX_DIAGNOSTIC
```

Both workflows use STS Mode 1, SDC, and 256 STS symbols. TX uses RF1. RX uses
PDoA Mode 3 with RF1-to-RF2 automatic switching and exports both 512-sample STS
accumulators as HCIR v3. The diagnostic deliberately skips FPGA configuration
and does not run the DO clock-control loop.

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
