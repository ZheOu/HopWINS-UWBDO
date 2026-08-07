/**
  ******************************************************************************
  * @file           : capture_output.h
  * @brief          : Selectable serial encoders for UWB receive captures
  ******************************************************************************
  */

#ifndef HOPWINS_CAPTURE_OUTPUT_H
#define HOPWINS_CAPTURE_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "board.h"
#include "uwb_timestamp_estimator.h"

typedef enum {
  SERIAL_CAPTURE_FORMAT_OFF = 0,
  SERIAL_CAPTURE_FORMAT_TEXT_V1,
  SERIAL_CAPTURE_FORMAT_HCIR_V2,
  SERIAL_CAPTURE_FORMAT_HCIR_V3,
} serial_capture_format_t;

void capture_output_init(void);
void capture_output_process(void);
board_status_t capture_output_start(
    const uwb_service_cir_capture_t *capture,
    const uwb_timestamp_result_t *timestamp,
    serial_capture_format_t format);
bool capture_output_busy(void);
bool capture_output_requires_cir(serial_capture_format_t format);
const char *capture_output_format_name(serial_capture_format_t format);
bool capture_output_format_is_supported(serial_capture_format_t format);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_CAPTURE_OUTPUT_H */
