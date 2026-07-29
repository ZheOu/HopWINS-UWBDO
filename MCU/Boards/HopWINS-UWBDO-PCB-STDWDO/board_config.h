/**
  ******************************************************************************
  * @file           : board_config.h
  * @brief          : Compile-time PCB assembly capabilities
  ******************************************************************************
  */

#ifndef HOPWINS_BOARD_CONFIG_H
#define HOPWINS_BOARD_CONFIG_H

/*
 * CMake selects one target with HOPWINS_BOARD_TARGET. Keep the capability
 * mapping here so component population remains owned by Boards rather than by
 * application roles.
 */
#define HOPWINS_BOARD_TARGET_LEADER_UWB_ONLY  1
#define HOPWINS_BOARD_TARGET_FOLLOWER_FULL    2

#define HOPWINS_BOARD_XO_NONE   0
#define HOPWINS_BOARD_XO_I2C    1
#define HOPWINS_BOARD_XO_CLKDP  2

#ifndef HOPWINS_BOARD_TARGET
#error "HOPWINS_BOARD_TARGET must be selected by a CMake configure preset"
#endif

#if HOPWINS_BOARD_TARGET == HOPWINS_BOARD_TARGET_LEADER_UWB_ONLY

#define HOPWINS_BOARD_NAME                        "Leader-UwbOnly"
#define HOPWINS_BOARD_HAS_FPGA                    0
#define HOPWINS_BOARD_HAS_CLOCK_CONTROL           0
#define HOPWINS_BOARD_HAS_EXTERNAL_CLOCK_COUNTER  0
#define HOPWINS_BOARD_DEFAULT_XO                  HOPWINS_BOARD_XO_NONE

#elif HOPWINS_BOARD_TARGET == HOPWINS_BOARD_TARGET_FOLLOWER_FULL

#define HOPWINS_BOARD_NAME                        "Follower-Full"
#define HOPWINS_BOARD_HAS_FPGA                    1
#define HOPWINS_BOARD_HAS_CLOCK_CONTROL           1
#define HOPWINS_BOARD_HAS_EXTERNAL_CLOCK_COUNTER  1
#define HOPWINS_BOARD_DEFAULT_XO                  HOPWINS_BOARD_XO_I2C

#else
#error "Unsupported HOPWINS_BOARD_TARGET"
#endif

#endif /* HOPWINS_BOARD_CONFIG_H */
