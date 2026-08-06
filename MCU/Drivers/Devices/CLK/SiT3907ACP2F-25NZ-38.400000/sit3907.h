/**
  ******************************************************************************
  * @file           : sit3907.h
  * @brief          : SiT3907ACP2F-25NZ-38.400000 DCXO driver
  ******************************************************************************
  *
  * This driver targets the exact ordering code named above. Its 38.4 MHz
  * nominal frequency, +/-1600 ppm pull range, address zero, and Mode 2 control
  * format are device constants rather than runtime configuration.
  *
  * Hardware integration requirements:
  * - DP must be driven high, driven low, and placed in true high impedance.
  * - An external bias network must hold a released DP pin in the valid middle
  *   input range (0.4 to 0.6 times the oscillator supply).
  * - Driven levels must satisfy the oscillator's VIL and VIH thresholds; an
  *   MCU high level below VIH is not a valid substitute for level translation.
  * - The line idles released. Do not emulate the middle level with push-pull.
  * - delay_us() must delay for at least the requested duration.
  *
  * The one-wire protocol has no acknowledgement or register readback. A
  * successful return value confirms only that a legal waveform was requested;
  * diagnose the physical path by measuring the output frequency.
  ******************************************************************************
  */

#ifndef SIT3907ACP2F_DRIVER_H
#define SIT3907ACP2F_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define SIT3907_NOMINAL_HZ        UINT32_C(38400000)
#define SIT3907_PULL_RANGE_PPM    UINT16_C(1600)
#define SIT3907_MAX_PULL_PPB      INT32_C(1600000)

typedef enum {
  SIT3907_STATUS_OK = 0,
  SIT3907_STATUS_BAD_ARG = -1,
  SIT3907_STATUS_NOT_INITIALIZED = -2,
  SIT3907_STATUS_OUT_OF_RANGE = -3,
} sit3907_status_t;

/** Platform callbacks for the tri-level DP interface. */
typedef struct {
  void (*dp_drive_high)(void);
  void (*dp_drive_low)(void);
  void (*dp_release)(void);
  void (*delay_us)(uint32_t delay_us);
} sit3907_platform_t;

typedef struct {
  bool initialized;
  const sit3907_platform_t *platform;
  int32_t frequency_control_word;
  int32_t pull_ppb;
} sit3907_device_t;

/** Validate the callbacks and park DP in its released idle state. */
sit3907_status_t sit3907_init(
    sit3907_device_t *device,
    const sit3907_platform_t *platform);

/** Apply a signed frequency pull in parts per billion. */
sit3907_status_t sit3907_set_pull_ppb(
    sit3907_device_t *device,
    int32_t ppb);

/** Restore the nominal 38.4 MHz output frequency. */
sit3907_status_t sit3907_center(sit3907_device_t *device);

#ifdef __cplusplus
}
#endif

#endif /* SIT3907ACP2F_DRIVER_H */
