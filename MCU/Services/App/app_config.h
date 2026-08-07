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

#include "capture_output.h"
#include "do_loop_strategy.h"
#include "dw3000.h"
#include "uwb_timestamp_estimator.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  APP_WORKFLOW_DO_LEADER = 0,
  APP_WORKFLOW_DO_FOLLOWER,
  APP_WORKFLOW_UWB_STS_TX_DIAGNOSTIC,
  APP_WORKFLOW_UWB_STS_DUAL_RX_DIAGNOSTIC,
} app_workflow_t;

typedef struct {
  app_workflow_t workflow;
  dw3000_rf_mode_t uwb_rf_mode;
  dw3000_pdoa_mode_t uwb_pdoa_mode;
  uint16_t cir_sample_count;
  uint16_t cir_pre_first_path_samples;
  bool do_clock_tracking;
  do_loop_strategy_id_t do_loop_strategy;
  uint32_t do_loop_window_intervals;
  uwb_timestamp_estimator_id_t timestamp_estimator;
  serial_capture_format_t follower_capture_format;
  bool follower_rf_ab_test;
  uint32_t follower_rf_ab_interval_ms;
} app_config_t;

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_APP_CONFIG_H */
