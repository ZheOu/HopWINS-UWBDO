/**
  ******************************************************************************
  * @file           : do_leader_service.c
  * @brief          : DO leader application role
  ******************************************************************************
  */

#include "role_service.h"

#include "board.h"
#include "console_service.h"
#include "uwb_service.h"

#ifndef HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
#define HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC 0
#endif

#if HOPWINS_APP_ROLE != HOPWINS_APP_ROLE_DO_LEADER
#error "do_leader_service.c compiled for the wrong application role"
#endif

const char *role_service_name(void)
{
  return "DO-Leader";
}

void role_service_init(void)
{
  dw3000_status_t status =
      uwb_service_init(board_uwb_get_platform());

#if HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
  if (status == DW3000_STATUS_OK) {
    (void)uwb_service_run_clock_diagnostic();
  }
#endif

  console_service_report_uwb(uwb_service_get_state());
  if (status != DW3000_STATUS_OK) {
    return;
  }

  (void)uwb_service_start_periodic_transmit(
      &g_uwb_default_profile);
  console_service_report_uwb_config(uwb_service_get_state());
}

void role_service_process(void)
{
  uwb_service_tx_event_t tx_event;

  uwb_service_process();
  if (uwb_service_take_tx_event(&tx_event)) {
    console_service_report_uwb_tx(&tx_event);
  }
}
