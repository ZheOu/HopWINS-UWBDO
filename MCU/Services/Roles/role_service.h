/**
  ******************************************************************************
  * @file           : role_service.h
  * @brief          : Selected application-role service interface
  ******************************************************************************
  */

#ifndef HOPWINS_ROLE_SERVICE_H
#define HOPWINS_ROLE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#define HOPWINS_APP_ROLE_DO_LEADER    1
#define HOPWINS_APP_ROLE_DO_FOLLOWER  2

#ifndef HOPWINS_APP_ROLE
#error "HOPWINS_APP_ROLE must be selected by a CMake configure preset"
#endif

const char *role_service_name(void);
void role_service_init(void);
void role_service_process(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_ROLE_SERVICE_H */
