/**
  ******************************************************************************
  * @file           : do_follower_service.h
  * @brief          : DO follower workflow interface
  ******************************************************************************
  */

#ifndef HOPWINS_DO_FOLLOWER_SERVICE_H
#define HOPWINS_DO_FOLLOWER_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_config.h"

void do_follower_service_init(const app_config_t *config);
void do_follower_service_process(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_DO_FOLLOWER_SERVICE_H */
