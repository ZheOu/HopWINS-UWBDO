/**
  ******************************************************************************
  * @file           : workflow_dispatcher.h
 * @brief          : Runtime application-workflow dispatcher
  ******************************************************************************
  */

#ifndef HOPWINS_WORKFLOW_DISPATCHER_H
#define HOPWINS_WORKFLOW_DISPATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_config.h"
#include <stdbool.h>

const char *workflow_dispatcher_name(app_workflow_t workflow);
bool workflow_dispatcher_init(const app_config_t *config);
void workflow_dispatcher_process(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_WORKFLOW_DISPATCHER_H */
