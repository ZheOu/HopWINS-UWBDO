/**
  ******************************************************************************
  * @file           : sit5156.h
  * @brief          : SiT5156AC-FA025JH-38.400000 DCTCXO driver
  ******************************************************************************
  *
  * This driver targets the exact ordering code named above. Its I2C address,
  * nominal frequency, pull range, and software output-enable behavior are
  * fixed device properties.
  *
  * Hardware integration requirements:
  * - Use an open-drain I2C bus with pull-ups to an electrically compatible rail.
  * - The platform callbacks transfer one-byte register addresses and raw data;
  *   this driver owns the 16-bit big-endian register packing.
  * - The FA025JH option uses software output enable and powers up disabled.
  * - I2C readback validates the control interface, but a counter or scope is
  *   still required to prove that the clock output reaches its destination.
  ******************************************************************************
  */

#ifndef SIT5156AC_DRIVER_H
#define SIT5156AC_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIT5156_I2C_ADDRESS_7BIT  UINT8_C(0x60)
#define SIT5156_NOMINAL_HZ        UINT32_C(38400000)
#define SIT5156_PULL_RANGE_PPM    UINT16_C(200)
#define SIT5156_MAX_PULL_PPB      INT32_C(200000)

typedef enum {
  SIT5156_STATUS_OK = 0,
  SIT5156_STATUS_BAD_ARG = -1,
  SIT5156_STATUS_IO_ERROR = -2,
  SIT5156_STATUS_NOT_INITIALIZED = -3,
  SIT5156_STATUS_OUT_OF_RANGE = -4,
  SIT5156_STATUS_UNEXPECTED_CONFIG = -5,
  SIT5156_STATUS_VERIFY_FAILED = -6,
} sit5156_status_t;

typedef struct {
  bool (*write)(uint8_t reg_address, const uint8_t *data, size_t len);
  bool (*read)(uint8_t reg_address, uint8_t *data, size_t len);
  void (*delay_us)(uint32_t delay_us);
} sit5156_platform_t;

/** Decoded register state retained for diagnostics. */
typedef struct {
  uint16_t frequency_lsw;
  uint16_t frequency_msw_oe;
  uint16_t pull_range_register;
  int32_t frequency_control_word;
  int32_t pull_ppb;
  bool output_enabled;
} sit5156_snapshot_t;

typedef struct {
  bool initialized;
  const sit5156_platform_t *platform;
  sit5156_snapshot_t snapshot;
} sit5156_device_t;

/** Read and validate the exact part's control registers without enabling CLK. */
sit5156_status_t sit5156_init(
    sit5156_device_t *device,
    const sit5156_platform_t *platform);

/** Set software output enable and verify the register readback. */
sit5156_status_t sit5156_set_output_enabled(
    sit5156_device_t *device,
    bool enabled);

/** Apply a signed frequency pull in parts per billion. */
sit5156_status_t sit5156_set_pull_ppb(
    sit5156_device_t *device,
    int32_t ppb);

/** Restore the nominal 38.4 MHz output frequency. */
sit5156_status_t sit5156_center(sit5156_device_t *device);

#ifdef __cplusplus
}
#endif

#endif /* SIT5156AC_DRIVER_H */
