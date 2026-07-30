/**
  ******************************************************************************
  * @file           : role_service.h
 * @brief          : Runtime application-workflow dispatcher
  ******************************************************************************
  */

#ifndef HOPWINS_ROLE_SERVICE_H
#define HOPWINS_ROLE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_config.h"

const char *role_service_name(app_workflow_t workflow);
bool role_service_init(const app_config_t *config);
void role_service_process(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_ROLE_SERVICE_H */
