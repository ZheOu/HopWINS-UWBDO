/**
  ******************************************************************************
  * @file           : workflow_dispatcher.c
  * @brief          : Runtime application-workflow dispatcher
  ******************************************************************************
  */

#include "workflow_dispatcher.h"

#include "board.h"
#include "do_clock_tracking_service.h"
#include "do_follower_service.h"
#include "do_leader_service.h"
#include "uwb_sts_diagnostic_service.h"
#include "uwb_profile.h"

static app_workflow_t s_workflow;
static bool s_initialized;
static uwb_profile_t s_uwb_profile;

static board_rf_path_mask_t required_rf_paths(
    dw3000_rf_mode_t mode)
{
  switch (mode) {
    case DW3000_RF_MODE_MANUAL_1:
      return BOARD_RF_PATH_1;
    case DW3000_RF_MODE_MANUAL_2:
      return BOARD_RF_PATH_2;
    case DW3000_RF_MODE_AUTO_1_2:
    case DW3000_RF_MODE_AUTO_2_1:
      return BOARD_RF_PATH_BOTH;
    default:
      return BOARD_RF_PATH_NONE;
  }
}

static bool configure_uwb_profile(
    const app_config_t *config,
    const board_description_t *board)
{
  board_rf_path_mask_t required_paths;
  bool automatic_rf_mode;
  bool pdoa_enabled;

  if ((config == NULL) || (board == NULL)) {
    return false;
  }
  s_uwb_profile = g_uwb_default_profile;
  if ((config->workflow == APP_WORKFLOW_UWB_STS_TX_DIAGNOSTIC) ||
      (config->workflow == APP_WORKFLOW_UWB_STS_DUAL_RX_DIAGNOSTIC)) {
    s_uwb_profile.radio.sts_mode = DW3000_STS_MODE_1;
    s_uwb_profile.radio.sts_length = 256U;
    s_uwb_profile.radio.sts_sdc = true;

    if (config->workflow == APP_WORKFLOW_UWB_STS_TX_DIAGNOSTIC) {
      s_uwb_profile.radio.rf_mode = DW3000_RF_MODE_MANUAL_1;
      s_uwb_profile.radio.pdoa_mode = DW3000_PDOA_MODE_DISABLED;
      required_paths = BOARD_RF_PATH_1;
    } else {
      s_uwb_profile.radio.rf_mode = DW3000_RF_MODE_AUTO_1_2;
      s_uwb_profile.radio.pdoa_mode = DW3000_PDOA_MODE_3;
      required_paths = BOARD_RF_PATH_BOTH;
    }
    return (board->available_rf_paths & required_paths) == required_paths;
  }

  if (!do_loop_strategy_is_supported(config->do_loop_strategy) ||
      (config->do_loop_window_intervals == 0U) ||
      (config->do_loop_window_intervals >
       DO_CLOCK_TRACKING_MAX_WINDOW_INTERVALS) ||
      !uwb_timestamp_estimator_is_supported(
          config->timestamp_estimator) ||
      !capture_output_format_is_supported(
          config->follower_capture_format)) {
    return false;
  }

  automatic_rf_mode =
      (config->uwb_rf_mode == DW3000_RF_MODE_AUTO_1_2) ||
      (config->uwb_rf_mode == DW3000_RF_MODE_AUTO_2_1);
  pdoa_enabled =
      (config->uwb_pdoa_mode == DW3000_PDOA_MODE_1) ||
      (config->uwb_pdoa_mode == DW3000_PDOA_MODE_3);
  if ((automatic_rf_mode != pdoa_enabled) ||
      (config->follower_rf_ab_test && pdoa_enabled)) {
    return false;
  }
  required_paths =
      ((config->workflow == APP_WORKFLOW_DO_FOLLOWER) &&
       config->follower_rf_ab_test)
          ? BOARD_RF_PATH_BOTH
          : required_rf_paths(config->uwb_rf_mode);
  if ((required_paths == BOARD_RF_PATH_NONE) ||
      ((board->available_rf_paths & required_paths) != required_paths)) {
    return false;
  }
  s_uwb_profile.radio.rf_mode = config->uwb_rf_mode;
  s_uwb_profile.radio.pdoa_mode = config->uwb_pdoa_mode;
  return true;
}

const char *workflow_dispatcher_name(app_workflow_t workflow)
{
  switch (workflow) {
    case APP_WORKFLOW_DO_LEADER:
      return "DO-Leader";
    case APP_WORKFLOW_DO_FOLLOWER:
      return "DO-Follower";
    case APP_WORKFLOW_UWB_STS_TX_DIAGNOSTIC:
      return "UWB-STS-TX-Diagnostic";
    case APP_WORKFLOW_UWB_STS_DUAL_RX_DIAGNOSTIC:
      return "UWB-STS-Dual-RX-Diagnostic";
    default:
      return "Invalid";
  }
}

bool workflow_dispatcher_init(const app_config_t *config)
{
  const board_description_t *board = board_get_description();

  s_initialized = false;
  if ((config == NULL) || (board == NULL)) {
    return false;
  }
  if (!configure_uwb_profile(config, board)) {
    return false;
  }

  s_workflow = config->workflow;
  switch (s_workflow) {
    case APP_WORKFLOW_DO_LEADER:
      do_leader_service_init(config, &s_uwb_profile);
      break;

    case APP_WORKFLOW_DO_FOLLOWER:
      if ((board->clock_device == BOARD_CLOCK_DEVICE_NONE) ||
          !board->external_clock_counter_connected ||
          (config->do_clock_tracking &&
           config->follower_rf_ab_test)) {
        return false;
      }
      do_follower_service_init(config, &s_uwb_profile);
      break;

    case APP_WORKFLOW_UWB_STS_TX_DIAGNOSTIC:
      uwb_sts_diagnostic_service_init(
          UWB_STS_DIAGNOSTIC_TX,
          config,
          &s_uwb_profile);
      break;

    case APP_WORKFLOW_UWB_STS_DUAL_RX_DIAGNOSTIC:
      uwb_sts_diagnostic_service_init(
          UWB_STS_DIAGNOSTIC_DUAL_RX,
          config,
          &s_uwb_profile);
      break;

    default:
      return false;
  }

  s_initialized = true;
  return true;
}

void workflow_dispatcher_process(void)
{
  if (!s_initialized) {
    return;
  }

  switch (s_workflow) {
    case APP_WORKFLOW_DO_LEADER:
      do_leader_service_process();
      break;
    case APP_WORKFLOW_DO_FOLLOWER:
      do_follower_service_process();
      break;
    case APP_WORKFLOW_UWB_STS_TX_DIAGNOSTIC:
    case APP_WORKFLOW_UWB_STS_DUAL_RX_DIAGNOSTIC:
      uwb_sts_diagnostic_service_process();
      break;
    default:
      s_initialized = false;
      break;
  }
}
