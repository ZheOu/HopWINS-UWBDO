/**
  ******************************************************************************
  * @file           : uwb_sts_diagnostic_service.h
  * @brief          : STS transmit and dual-antenna receive diagnostic workflow
  ******************************************************************************
  */

#ifndef HOPWINS_UWB_STS_DIAGNOSTIC_SERVICE_H
#define HOPWINS_UWB_STS_DIAGNOSTIC_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_config.h"
#include "uwb_profile.h"

typedef enum {
  UWB_STS_DIAGNOSTIC_TX = 0,
  UWB_STS_DIAGNOSTIC_DUAL_RX,
} uwb_sts_diagnostic_role_t;

void uwb_sts_diagnostic_service_init(
    uwb_sts_diagnostic_role_t role,
    const app_config_t *config,
    const uwb_profile_t *profile);
void uwb_sts_diagnostic_service_process(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_UWB_STS_DIAGNOSTIC_SERVICE_H */
