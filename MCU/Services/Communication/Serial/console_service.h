/**
  ******************************************************************************
  * @file           : console_service.h
  * @brief          : PC console reporting service
  ******************************************************************************
  */

#ifndef HOPWINS_CONSOLE_SERVICE_H
#define HOPWINS_CONSOLE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "board.h"
#include "capture_output.h"
#include "clock_service.h"
#include "do_clock_tracking_service.h"
#include "fpga_service.h"
#include "uwb_service.h"

void console_service_init(void);
void console_service_finish_boot(void);
void console_service_process(void);
void console_service_write(const char *text);
void console_service_report_firmware_profile(
    const board_description_t *board,
    const char *role_name,
    const char *build_type);
void console_service_report_clock(const clock_service_state_t *state);
void console_service_report_fpga_image(const fpga_service_state_t *state);
void console_service_report_fpga_result(const fpga_service_state_t *state);
void console_service_report_uwb(const uwb_service_state_t *state);
void console_service_report_uwb_config(const uwb_service_state_t *state);
void console_service_report_uwb_rx_health(
    const uwb_service_state_t *state);
void console_service_report_uwb_tx(
    const uwb_service_tx_event_t *event);
void console_service_report_do_clock_tracking_config(
    const do_clock_tracking_state_t *state,
    const uwb_timestamp_estimator_state_t *timestamp_state,
    serial_capture_format_t capture_format);
void console_service_report_do_clock_tracking(
    const do_clock_tracking_event_t *event,
    const do_clock_tracking_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_CONSOLE_SERVICE_H */
