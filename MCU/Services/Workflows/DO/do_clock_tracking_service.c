/**
  ******************************************************************************
  * @file           : do_clock_tracking_service.c
  * @brief          : Timestamp-slope clock tracking for the DO follower
  ******************************************************************************
  */

#include "do_clock_tracking_service.h"

#include <stddef.h>
#include <string.h>

#define DO_FRAME_MIN_LEN UINT16_C(21)
#define DO_FRAME_MAGIC_OFFSET UINT16_C(9)
#define DO_FRAME_SEQUENCE_OFFSET UINT16_C(13)
#define DO_FRAME_SCHEDULE_OFFSET UINT16_C(17)
#define DO_RX_TIMESTAMP_MASK UINT64_C(0xFFFFFFFFFF)
#define DO_RX_TIMESTAMP_HALF_RANGE UINT64_C(0x8000000000)
#define DO_SEQUENCE_HALF_RANGE UINT32_C(0x80000000)
#define DO_SCHEDULE_TO_TIMESTAMP_SHIFT 8U
#define PPB_PER_UNIT UINT64_C(1000000000)

typedef struct {
  uint32_t sequence;
  uint32_t scheduled_time;
} do_transmit_time_t;

static do_clock_tracking_state_t s_state;
static do_loop_strategy_t s_strategy;

static bool parse_transmit_time(
    const uwb_service_cir_capture_t *capture,
    do_transmit_time_t *transmit_time);
static uint32_t read_u32_le(const uint8_t *source);
static int32_t estimate_error_ppb(
    int64_t timestamp_error_dtu,
    uint64_t leader_delta_dtu);
static int32_t clamp_command(
    int64_t command_ppb,
    int32_t command_limit_ppb);
static void set_reference(
    const do_transmit_time_t *transmit_time,
    uint64_t receive_timestamp);

bool do_clock_tracking_service_init(
    const do_clock_tracking_config_t *config)
{
  const clock_service_state_t *clock = clock_service_get_state();

  memset(&s_state, 0, sizeof(s_state));
  if ((config == NULL) || (config->window_intervals == 0U) ||
      (config->window_intervals >
       DO_CLOCK_TRACKING_MAX_WINDOW_INTERVALS) ||
      !do_loop_strategy_init(&s_strategy, config->strategy)) {
    return false;
  }
  s_state.enabled = config->enabled;
  s_state.strategy = config->strategy;
  s_state.window_intervals = config->window_intervals;
  s_state.clock_status = CLOCK_SERVICE_STATUS_OK;

  if (!config->enabled) {
    s_state.initialized = true;
    return true;
  }
  if ((clock == NULL) || !clock->initialized ||
      (clock->clock_device == BOARD_CLOCK_DEVICE_NONE) ||
      (clock->pull_limit_ppb <= 0)) {
    s_state.clock_status = CLOCK_SERVICE_STATUS_NO_DEVICE;
    return false;
  }

  s_state.command_ppb = clock->pull_ppb;
  s_state.command_limit_ppb = clock->pull_limit_ppb;
  s_state.initialized = true;
  return true;
}

bool do_clock_tracking_service_process_capture(
    const uwb_service_cir_capture_t *capture,
    uint64_t receive_timestamp,
    do_clock_tracking_event_t *event)
{
  do_transmit_time_t transmit_time;
  clock_service_status_t clock_status;
  uint32_t interval_count;
  uint32_t scheduled_delta;
  uint64_t leader_delta_dtu;
  uint64_t follower_delta_dtu;
  uint64_t maximum_error_dtu;
  int64_t timestamp_error_dtu;
  int32_t measured_error_ppb;
  int32_t next_command_ppb;

  if (event != NULL) {
    *event = (do_clock_tracking_event_t){0};
  }
  if (!s_state.enabled || !s_state.initialized ||
      !parse_transmit_time(capture, &transmit_time)) {
    if (s_state.enabled && s_state.initialized) {
      s_state.ignored_frame_count++;
    }
    return false;
  }

  s_state.accepted_frame_count++;
  if (!s_state.reference_valid) {
    set_reference(&transmit_time, receive_timestamp);
    return false;
  }

  interval_count = transmit_time.sequence - s_state.reference_sequence;
  if ((interval_count == 0U) ||
      (interval_count >= DO_SEQUENCE_HALF_RANGE)) {
    s_state.ignored_frame_count++;
    set_reference(&transmit_time, receive_timestamp);
    return false;
  }
  scheduled_delta =
      transmit_time.scheduled_time - s_state.reference_scheduled_time;
  /* DX_TIME carries timestamp bits 8..39. Restoring the low eight zero bits
     puts Leader elapsed time on the same 40-bit DTU scale as RX_TIME. */
  leader_delta_dtu =
      (uint64_t)scheduled_delta << DO_SCHEDULE_TO_TIMESTAMP_SHIFT;
  follower_delta_dtu =
      (receive_timestamp - s_state.reference_receive_timestamp) &
      DO_RX_TIMESTAMP_MASK;

  if ((leader_delta_dtu == 0U) ||
      (leader_delta_dtu >= DO_RX_TIMESTAMP_HALF_RANGE) ||
      (follower_delta_dtu >= DO_RX_TIMESTAMP_HALF_RANGE)) {
    s_state.rejected_window_count++;
    set_reference(&transmit_time, receive_timestamp);
    return false;
  }

  timestamp_error_dtu =
      (int64_t)follower_delta_dtu - (int64_t)leader_delta_dtu;
  maximum_error_dtu =
      (leader_delta_dtu * DO_CLOCK_TRACKING_MAX_ERROR_PPB) /
      PPB_PER_UNIT;
  if ((timestamp_error_dtu > (int64_t)maximum_error_dtu) ||
      (timestamp_error_dtu < -(int64_t)maximum_error_dtu)) {
    if (interval_count < s_state.window_intervals) {
      return false;
    }
    s_state.rejected_window_count++;
    if (event != NULL) {
      *event = (do_clock_tracking_event_t){
        .type = DO_CLOCK_TRACKING_EVENT_OUTLIER,
        .clock_status = CLOCK_SERVICE_STATUS_OK,
        .reference_sequence = s_state.reference_sequence,
        .sequence = transmit_time.sequence,
        .interval_count = interval_count,
        .leader_delta_dtu = leader_delta_dtu,
        .follower_delta_dtu = follower_delta_dtu,
        .command_ppb = s_state.command_ppb,
      };
    }
    set_reference(&transmit_time, receive_timestamp);
    return event != NULL;
  }

  measured_error_ppb = estimate_error_ppb(
      timestamp_error_dtu,
      leader_delta_dtu);
  if (!do_loop_strategy_observe(
          &s_strategy,
          interval_count,
          measured_error_ppb,
          interval_count >= s_state.window_intervals,
          &measured_error_ppb)) {
    return false;
  }
  s_state.last_strategy_observation_count =
      s_strategy.observation_count;
  next_command_ppb = clamp_command(
      (int64_t)s_state.command_ppb - measured_error_ppb,
      s_state.command_limit_ppb);
  clock_status = clock_service_set_pull_ppb(next_command_ppb);

  s_state.last_error_ppb = measured_error_ppb;
  s_state.clock_status = clock_status;
  if (clock_status == CLOCK_SERVICE_STATUS_OK) {
    s_state.command_ppb = next_command_ppb;
    s_state.update_count++;
  }
  if (event != NULL) {
    *event = (do_clock_tracking_event_t){
      .type = (clock_status == CLOCK_SERVICE_STATUS_OK)
                  ? DO_CLOCK_TRACKING_EVENT_UPDATE
                  : DO_CLOCK_TRACKING_EVENT_CLOCK_ERROR,
      .clock_status = clock_status,
      .reference_sequence = s_state.reference_sequence,
      .sequence = transmit_time.sequence,
      .interval_count = interval_count,
      .leader_delta_dtu = leader_delta_dtu,
      .follower_delta_dtu = follower_delta_dtu,
      .measured_error_ppb = measured_error_ppb,
      .command_ppb = s_state.command_ppb,
    };
  }
  set_reference(&transmit_time, receive_timestamp);
  return event != NULL;
}

const do_clock_tracking_state_t *do_clock_tracking_service_get_state(void)
{
  return &s_state;
}

static bool parse_transmit_time(
    const uwb_service_cir_capture_t *capture,
    do_transmit_time_t *transmit_time)
{
  const uint8_t *frame;

  if ((capture == NULL) || (transmit_time == NULL) ||
      (capture->frame == NULL) ||
      (capture->frame_len < DO_FRAME_MIN_LEN)) {
    return false;
  }

  frame = capture->frame;
  if (memcmp(&frame[DO_FRAME_MAGIC_OFFSET], "HWDO", 4U) != 0) {
    return false;
  }

  transmit_time->sequence = read_u32_le(
      &frame[DO_FRAME_SEQUENCE_OFFSET]);
  transmit_time->scheduled_time = read_u32_le(
      &frame[DO_FRAME_SCHEDULE_OFFSET]);
  return true;
}

static uint32_t read_u32_le(const uint8_t *source)
{
  return (uint32_t)source[0] |
      ((uint32_t)source[1] << 8U) |
      ((uint32_t)source[2] << 16U) |
      ((uint32_t)source[3] << 24U);
}

static int32_t estimate_error_ppb(
    int64_t timestamp_error_dtu,
    uint64_t leader_delta_dtu)
{
  int64_t numerator = timestamp_error_dtu * (int64_t)PPB_PER_UNIT;
  int64_t rounding = (int64_t)(leader_delta_dtu / 2U);

  if (numerator >= 0) {
    numerator += rounding;
  } else {
    numerator -= rounding;
  }
  return (int32_t)(numerator / (int64_t)leader_delta_dtu);
}

static int32_t clamp_command(
    int64_t command_ppb,
    int32_t command_limit_ppb)
{
  if (command_ppb > command_limit_ppb) {
    return command_limit_ppb;
  }
  if (command_ppb < -command_limit_ppb) {
    return -command_limit_ppb;
  }
  return (int32_t)command_ppb;
}

static void set_reference(
    const do_transmit_time_t *transmit_time,
    uint64_t receive_timestamp)
{
  s_state.reference_sequence = transmit_time->sequence;
  s_state.reference_scheduled_time = transmit_time->scheduled_time;
  s_state.reference_receive_timestamp =
      receive_timestamp & DO_RX_TIMESTAMP_MASK;
  s_state.reference_valid = true;
  do_loop_strategy_reset(&s_strategy);
}
