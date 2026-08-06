/**
  ******************************************************************************
  * @file           : app.h
  * @brief          : Top-level application service
  ******************************************************************************
  */

#ifndef HOPWINS_APP_H
#define HOPWINS_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_config.h"
#include <stdbool.h>

bool app_init(const app_config_t *config);
void app_process(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_APP_H */
