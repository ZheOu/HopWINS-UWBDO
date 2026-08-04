/**
  ******************************************************************************
  * @file           : sit5156.h
  * @brief          : SiT5156AC-FA025JH-38.400000 DCTCXO driver
  ******************************************************************************
  */

#ifndef HOPWINS_SIT5156_H
#define HOPWINS_SIT5156_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Values decoded from the exact fitted-part ordering code. */
#define SIT5156_I2C_ADDRESS_7BIT UINT8_C(0x60)
#define SIT5156_NOMINAL_HZ       UINT32_C(38400000)
#define SIT5156_PULL_RANGE_PPM   UINT16_C(200)
#define SIT5156_PULL_RANGE_CODE  UINT8_C(0x09)

typedef enum {
  SIT5156_STATUS_OK = 0,
  SIT5156_STATUS_BAD_ARG = -1,
  SIT5156_STATUS_IO_ERROR = -2,
  SIT5156_STATUS_NOT_INITIALIZED = -3,
  SIT5156_STATUS_OUT_OF_RANGE = -4,
  SIT5156_STATUS_UNEXPECTED_CONFIG = -5,
  SIT5156_STATUS_VERIFY_FAILED = -6,
} sit5156_status_t;

/**
  * @brief Bus hooks supplied by the board/service layer.
  *
  * The SiT5156 register address is one byte. Register values are 16-bit and
  * transferred most-significant byte first; byte packing stays in this driver.
  */
typedef struct {
  bool (*write)(uint8_t reg_address, const uint8_t *data, size_t len);
  bool (*read)(uint8_t reg_address, uint8_t *data, size_t len);
  void (*delay_us)(uint32_t delay_us);
} sit5156_platform_t;

typedef struct {
  uint16_t frequency_lsw;
  uint16_t frequency_msw_oe;
  uint16_t pull_range;
  int32_t frequency_control_word;
  int32_t pull_ppb;
  bool output_enabled;
} sit5156_snapshot_t;

typedef struct {
  bool initialized;
  const sit5156_platform_t *platform;
  sit5156_snapshot_t snapshot;
  uint32_t writes;
} sit5156_device_t;

/**
  * @brief Read and validate the three control registers of the fitted part.
  *
  * This does not enable CLK. The fitted J-option uses software OE, whose
  * power-up default is disabled; call sit5156_set_output_enabled() explicitly.
  */
sit5156_status_t sit5156_init(
    sit5156_device_t *device,
    const sit5156_platform_t *platform);

/** @brief Refresh the cached register snapshot from the device. */
sit5156_status_t sit5156_refresh(sit5156_device_t *device);

/** @brief Control register 0x01[10], then read it back for verification. */
sit5156_status_t sit5156_set_output_enabled(
    sit5156_device_t *device,
    bool enabled);

/** @brief Pull the nominal output by signed parts per billion. */
sit5156_status_t sit5156_set_pull_ppb(
    sit5156_device_t *device,
    int32_t ppb);

/** @brief Write a signed 26-bit frequency-control word directly. */
sit5156_status_t sit5156_set_pull_code(
    sit5156_device_t *device,
    int32_t code);

/** @brief Restore the nominal 38.4 MHz output frequency. */
sit5156_status_t sit5156_center(sit5156_device_t *device);

/** @brief Convert ppb to the signed 26-bit frequency-control word, no I/O. */
sit5156_status_t sit5156_ppb_to_code(int32_t ppb, int32_t *code);

/** @brief Convert a signed frequency-control word back to ppb, no I/O. */
sit5156_status_t sit5156_code_to_ppb(int32_t code, int32_t *ppb);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_SIT5156_H */
