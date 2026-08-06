/**
  ******************************************************************************
  * @file           : ice40up5k.h
  * @brief          : iCE40UP5K-SG48I SPI configuration driver
  ******************************************************************************
  *
  * This driver implements SPI peripheral configuration for the exact target
  * named above. It owns the configuration sequence and bitstream signature
  * checks; image storage, retry policy, logging, and board pin assignments
  * belong to higher layers.
  *
  * Hardware integration requirements:
  * - CRESET_B must be asserted Low and released High by set_creset_b().
  * - CDONE is open-drain and requires a pull-up. A live device must pull it Low
  *   while CRESET_B is asserted.
  * - SPI must be MSB-first, mode 0 or mode 3, and remain within the device's
  *   configuration-clock limits.
  * - spi_select() must acquire the bus and assert SPI_SS. spi_write() must
  *   accept the complete image, split it when the platform transport requires,
  *   and leave SPI_SS unchanged throughout the transfer.
  * - delay_us() must delay for at least the requested duration.
  *
  * Image validation checks the Radiant target marker and synchronization word;
  * CDONE remains the authoritative result of the FPGA's internal image check.
  ******************************************************************************
  */

#ifndef ICE40UP5K_DRIVER_H
#define ICE40UP5K_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  ICE40UP5K_STATUS_OK = 0,
  ICE40UP5K_STATUS_BAD_ARG = -1,
  ICE40UP5K_STATUS_BAD_IMAGE = -2,
  ICE40UP5K_STATUS_SPI_ERROR = -3,
  ICE40UP5K_STATUS_CDONE_STUCK_HIGH = -4,
  ICE40UP5K_STATUS_CDONE_TIMEOUT = -5,
  ICE40UP5K_STATUS_CDONE_DROPPED = -6,
} ice40up5k_status_t;

typedef struct ice40up5k_platform {
  void (*set_creset_b)(bool high);
  bool (*read_cdone)(void);
  bool (*spi_select)(void);
  void (*spi_deselect)(void);
  bool (*spi_write)(const uint8_t *data, size_t len);
  void (*delay_us)(uint32_t delay_us);
} ice40up5k_platform_t;

typedef struct {
  const uint8_t *data;
  uint32_t length;
} ice40up5k_image_t;

/** Result and diagnostics from one complete configuration attempt. */
typedef struct {
  bool cdone_at_reset;
  uint32_t cdone_clocks;
} ice40up5k_result_t;

/** Validate image size, target marker, and synchronization word. */
ice40up5k_status_t ice40up5k_validate_image(
    const ice40up5k_image_t *image,
    uint32_t *sync_offset);

/** Run one SPI peripheral configuration attempt. */
ice40up5k_status_t ice40up5k_configure(
    ice40up5k_result_t *result,
    const ice40up5k_platform_t *platform,
    const ice40up5k_image_t *image);

#ifdef __cplusplus
}
#endif

#endif /* ICE40UP5K_DRIVER_H */
