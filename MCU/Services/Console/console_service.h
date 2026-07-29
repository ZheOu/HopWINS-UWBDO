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
#include "fpga_service.h"
#include "uwb_service.h"

void console_service_init(void);
void console_service_process(void);
void console_service_write(const char *text);
void console_service_report_firmware_profile(
    const board_capabilities_t *board,
    const char *role_name,
    const char *build_type);
void console_service_report_fpga_image(const fpga_service_state_t *state);
void console_service_report_fpga_result(const fpga_service_state_t *state);
void console_service_report_uwb(const uwb_service_state_t *state);
void console_service_report_uwb_config(const uwb_service_state_t *state);
void console_service_report_uwb_tx(
    const uwb_service_tx_event_t *event);
board_status_t console_service_start_uwb_cir_export(
    const uwb_service_cir_capture_t *capture);
bool console_service_uwb_cir_export_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_CONSOLE_SERVICE_H */
