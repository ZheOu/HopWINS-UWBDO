/**
  ******************************************************************************
  * @file           : clock_service.h
  * @brief          : Board-selected local clock control and verification
  ******************************************************************************
  */

#ifndef HOPWINS_CLOCK_SERVICE_H
#define HOPWINS_CLOCK_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "board.h"

typedef enum {
  CLOCK_SERVICE_STATUS_OK = 0,
  CLOCK_SERVICE_STATUS_BAD_ARG = -1,
  CLOCK_SERVICE_STATUS_NO_DEVICE = -2,
  CLOCK_SERVICE_STATUS_IO_ERROR = -3,
  CLOCK_SERVICE_STATUS_UNEXPECTED_CONFIG = -4,
  CLOCK_SERVICE_STATUS_VERIFY_FAILED = -5,
  CLOCK_SERVICE_STATUS_NOT_INITIALIZED = -6,
  CLOCK_SERVICE_STATUS_OUT_OF_RANGE = -7,
} clock_service_status_t;

typedef struct {
  clock_service_status_t status;
  board_clock_device_t clock_device;
  const char *part_name;
  bool initialized;
  bool register_readback_verified;
  bool output_enabled;
  bool reference_counter_checked;
  bool output_clock_detected;
  uint8_t control_address_7bit;
  uint16_t frequency_lsw;
  uint16_t frequency_msw_oe;
  uint16_t pull_range_register;
  int32_t frequency_control_word;
  int32_t pull_ppb;
  int32_t pull_limit_ppb;
  uint32_t reference_counter_delta;
} clock_service_state_t;

/**
  * @brief Initialize the XO selected by board_init() and verify its output.
  *
  * The SiT5156 path restores nominal frequency, enables software OE, reads all
  * values back, waits for rated stability, and checks that the Board reference
  * counter advances.
  */
clock_service_status_t clock_service_init(void);

clock_service_status_t clock_service_set_pull_ppb(int32_t ppb);
const clock_service_state_t *clock_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_CLOCK_SERVICE_H */
