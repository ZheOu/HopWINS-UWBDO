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
#include "uwb_profile.h"

void do_follower_service_init(
    const app_config_t *config,
    const uwb_profile_t *uwb_profile);
void do_follower_service_process(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_DO_FOLLOWER_SERVICE_H */
