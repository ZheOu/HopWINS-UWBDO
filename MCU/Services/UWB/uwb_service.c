/**
  ******************************************************************************
  * @file           : uwb_service.c
  * @brief          : DW3000 initialization and diagnostics service
  ******************************************************************************
  */

#include "uwb_service.h"

#include <stddef.h>

static const dw3000_platform_t *s_platform;
static uwb_service_state_t s_state;

dw3000_status_t uwb_service_init(const dw3000_platform_t *platform)
{
  s_platform = platform;
  s_state = (uwb_service_state_t){
    .init_status = DW3000_STATUS_BAD_ARG,
    .clock_status = DW3000_STATUS_NOT_READY,
  };

  if (s_platform == NULL) {
    return s_state.init_status;
  }

  s_state.init_status = dw3000_init(&s_state.device, s_platform);
  return s_state.init_status;
}

dw3000_status_t uwb_service_run_clock_diagnostic(void)
{
  s_state.clock_diagnostic_run = true;

  if ((s_platform == NULL) || (s_state.init_status != DW3000_STATUS_OK)) {
    s_state.clock_status = DW3000_STATUS_NOT_READY;
    return s_state.clock_status;
  }

  s_state.clock_status = dw3000_run_clock_diagnostic(
      &s_state.device,
      &s_state.clock_diagnostic);
  return s_state.clock_status;
}

const uwb_service_state_t *uwb_service_get_state(void)
{
  return &s_state;
}
