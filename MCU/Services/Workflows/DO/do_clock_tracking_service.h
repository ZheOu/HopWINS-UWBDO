/**
  ******************************************************************************
  * @file           : do_clock_tracking_service.h
  * @brief          : Timestamp-slope clock tracking for the DO follower
  ******************************************************************************
  */

#ifndef HOPWINS_DO_CLOCK_TRACKING_SERVICE_H
#define HOPWINS_DO_CLOCK_TRACKING_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "clock_service.h"
#include "do_loop_strategy.h"
#include "uwb_service.h"

#define DO_CLOCK_TRACKING_DEFAULT_WINDOW_INTERVALS UINT32_C(20)
#define DO_CLOCK_TRACKING_MAX_WINDOW_INTERVALS UINT32_C(10000)
#define DO_CLOCK_TRACKING_MAX_ERROR_PPB UINT32_C(200000)

typedef struct {
  bool enabled;
  do_loop_strategy_id_t strategy;
  uint32_t window_intervals;
} do_clock_tracking_config_t;

typedef enum {
  DO_CLOCK_TRACKING_EVENT_NONE = 0,
  DO_CLOCK_TRACKING_EVENT_UPDATE,
  DO_CLOCK_TRACKING_EVENT_OUTLIER,
  DO_CLOCK_TRACKING_EVENT_CLOCK_ERROR,
} do_clock_tracking_event_type_t;

typedef struct {
  do_clock_tracking_event_type_t type;
  clock_service_status_t clock_status;
  uint32_t reference_sequence;
  uint32_t sequence;
  uint32_t interval_count;
  uint64_t leader_delta_dtu;
  uint64_t follower_delta_dtu;
  int32_t measured_error_ppb;
  int32_t command_ppb;
} do_clock_tracking_event_t;

typedef struct {
  bool enabled;
  bool initialized;
  bool reference_valid;
  do_loop_strategy_id_t strategy;
  uint32_t window_intervals;
  uint32_t last_strategy_observation_count;
  uint32_t accepted_frame_count;
  uint32_t ignored_frame_count;
  uint32_t update_count;
  uint32_t rejected_window_count;
  uint32_t reference_sequence;
  uint32_t reference_scheduled_time;
  uint64_t reference_receive_timestamp;
  int32_t last_error_ppb;
  int32_t command_ppb;
  int32_t command_limit_ppb;
  clock_service_status_t clock_status;
} do_clock_tracking_state_t;

bool do_clock_tracking_service_init(
    const do_clock_tracking_config_t *config);
bool do_clock_tracking_service_process_capture(
    const uwb_service_cir_capture_t *capture,
    uint64_t receive_timestamp,
    do_clock_tracking_event_t *event);
const do_clock_tracking_state_t *do_clock_tracking_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_DO_CLOCK_TRACKING_SERVICE_H */
