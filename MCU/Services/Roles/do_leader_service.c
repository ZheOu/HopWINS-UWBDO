/**
  ******************************************************************************
  * @file           : do_leader_service.c
  * @brief          : DO leader application role
  ******************************************************************************
  */

#include "do_leader_service.h"

#include "board.h"
#include "console_service.h"
#include "uwb_service.h"

void do_leader_service_init(const app_config_t *config)
{
  dw3000_status_t status =
      uwb_service_init(board_uwb_get_platform());

  if ((status == DW3000_STATUS_OK) &&
      config->run_boot_uwb_clock_diagnostic) {
    (void)uwb_service_run_clock_diagnostic();
  }

  console_service_report_uwb(uwb_service_get_state());
  if (status != DW3000_STATUS_OK) {
    return;
  }

  (void)uwb_service_start_periodic_transmit(
      &g_uwb_default_profile);
  console_service_report_uwb_config(uwb_service_get_state());
}

void do_leader_service_process(void)
{
  uwb_service_tx_event_t tx_event;

  uwb_service_process();
  if (uwb_service_take_tx_event(&tx_event)) {
    console_service_report_uwb_tx(&tx_event);
  }
}
