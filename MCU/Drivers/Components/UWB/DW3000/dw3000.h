/**
  ******************************************************************************
  * @file           : dw3000.h
  * @brief          : Project-facing DW3000-family driver interface
  ******************************************************************************
  */

#ifndef HOPWINS_DW3000_H
#define HOPWINS_DW3000_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define DW3000_DEVICE_ID_MASK     UINT32_C(0xFFFFFF0F)
#define DW3000_DEVICE_ID_EXPECTED UINT32_C(0xDECA0302)

typedef enum {
  DW3000_STATUS_OK = 0,
  DW3000_STATUS_BAD_ARG = -1,
  DW3000_STATUS_PLATFORM_ERROR = -2,
  DW3000_STATUS_SPI_ERROR = -3,
  DW3000_STATUS_UNSUPPORTED_DEVICE = -4,
  DW3000_STATUS_NOT_READY = -5,
  DW3000_STATUS_INITIALIZATION_ERROR = -6,
  DW3000_STATUS_CLOCK_ERROR = -7,
} dw3000_status_t;

typedef struct dw3000_platform {
  int32_t (*hardware_reset)(void);
  int32_t (*spi_set_slow_rate)(void);
  int32_t (*spi_set_fast_rate)(void);
  int32_t (*spi_read)(
      const uint8_t *header,
      uint16_t header_len,
      uint8_t *data,
      uint16_t data_len);
  int32_t (*spi_write)(
      const uint8_t *header,
      uint16_t header_len,
      const uint8_t *data,
      uint16_t data_len,
      const uint8_t *trailer,
      uint16_t trailer_len);
  void (*delay_ms)(uint32_t delay_ms);
  void (*delay_us)(uint32_t delay_us);
  int32_t (*lock)(void);
  void (*unlock)(int32_t lock_state);
} dw3000_platform_t;

typedef struct {
  uint32_t device_id;
  bool initialized;
} dw3000_device_t;

/* Temporary reference-clock diagnostic; remove after board clock validation. */
typedef struct {
  int32_t pll_result;
  int32_t restore_result;
  uint32_t pll_status;
  bool xtal_settled;
  bool pll_locked;
  bool calibration_done;
} dw3000_clock_diagnostic_t;

dw3000_status_t dw3000_init(
    dw3000_device_t *device,
    const dw3000_platform_t *platform);
dw3000_status_t dw3000_read_device_id(
    dw3000_device_t *device,
    uint32_t *device_id);
bool dw3000_is_supported_device_id(uint32_t device_id);
dw3000_status_t dw3000_run_clock_diagnostic(
    const dw3000_device_t *device,
    dw3000_clock_diagnostic_t *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_DW3000_H */
