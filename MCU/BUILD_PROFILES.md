# MCU Build Profiles

Firmware selection has three independent dimensions:

| Dimension | CMake variable | Current values |
|---|---|---|
| Physical PCB | `HOPWINS_BOARD_TARGET` | `LEADER_UWB_ONLY`, `FOLLOWER_FULL` |
| Firmware workflow | `HOPWINS_APP_ROLE` | `DO_LEADER`, `DO_FOLLOWER` |
| Optimization/debug info | `CMAKE_BUILD_TYPE` | `Debug`, `Release` |

PCB capabilities are defined in
`Boards/HopWINS-UWBDO-PCB-STDWDO/board_config.h`; the adjacent
`board_target.cmake` owns their CMake selection and FPGA-image policy. The
selected role source implements the common `Roles/role_service.h` interface.
`App/app.c` therefore does not contain role-specific conditionals.

## Product presets

| Configure/build preset | Output ELF | Behavior |
|---|---|---|
| `Leader-Debug` | `build/Leader-Debug/HopWINS-DO-Leader.elf` | UWB-only PCB, periodic DO TX |
| `Leader-Release` | `build/Leader-Release/HopWINS-DO-Leader.elf` | Same product, optimized |
| `Follower-Debug` | `build/Follower-Debug/HopWINS-DO-Follower.elf` | Full PCB, FPGA startup and UWB RX/CIR |
| `Follower-Release` | `build/Follower-Release/HopWINS-DO-Follower.elf` | Same product, optimized |

Leader builds do not embed the FPGA image or compile the FPGA driver/service.
Follower builds require FPGA and clock-control capabilities. CMake rejects an
incompatible board/role selection.

Debug and Release select compiler optimization and debug information only.
Optional startup diagnostics remain separately controlled by
`HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC`.

## VS Code

Open the repository root. CMake Tools reads `MCU/CMakePresets.json` because
the root `.vscode/settings.json` sets `MCU` as the source directory.

The Run and Debug view has two fixed entries:

- `STM32: DO Leader Debug`
- `STM32: DO Follower Debug`

Each launch configuration runs an exact configure/build task and then loads
the corresponding ELF, so another active CMake preset cannot cause the wrong
firmware to be programmed.

## Runtime modes

DO operation is selected by the compiled role. Future occasional TWR
calibration belongs inside each role's runtime state machine:

```text
Leader:   DO TX -> TWR calibration -> DO TX
Follower: DO RX/CIR -> TWR calibration -> DO RX/CIR
```

TWR should not require another PCB target, preset, or ELF.
