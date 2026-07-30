/**
  ******************************************************************************
  * @file           : app_config.h
  * @brief          : Firmware workflow configuration
  ******************************************************************************
  */

#ifndef HOPWINS_APP_CONFIG_H
#define HOPWINS_APP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  APP_WORKFLOW_DO_LEADER = 0,
  APP_WORKFLOW_DO_FOLLOWER,
} app_workflow_t;

typedef struct {
  app_workflow_t workflow;
  bool run_boot_uwb_clock_diagnostic;
  uint16_t cir_sample_count;
  uint16_t cir_pre_first_path_samples;
} app_config_t;

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_APP_CONFIG_H */
