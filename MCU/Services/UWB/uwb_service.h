/**
  ******************************************************************************
  * @file           : uwb_service.h
  * @brief          : DW3000 initialization and diagnostics service
  ******************************************************************************
  */

#ifndef HOPWINS_UWB_SERVICE_H
#define HOPWINS_UWB_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dw3000.h"

typedef struct {
  dw3000_device_t device;
  dw3000_status_t init_status;
  dw3000_clock_diagnostic_t clock_diagnostic;
  dw3000_status_t clock_status;
  bool clock_diagnostic_run;
} uwb_service_state_t;

dw3000_status_t uwb_service_init(const dw3000_platform_t *platform);
dw3000_status_t uwb_service_run_clock_diagnostic(void);
const uwb_service_state_t *uwb_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_UWB_SERVICE_H */
