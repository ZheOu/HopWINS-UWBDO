/**
  ******************************************************************************
  * @file           : uwb_service.c
  * @brief          : DW3000 initialization, diagnostics, and TX service
  ******************************************************************************
  */

#include "uwb_service.h"

#include <stddef.h>
#include <string.h>

#define UWB_FRAME_CONTROL            UINT16_C(0x8841)
#define UWB_MAC_HEADER_LEN           9U
#define UWB_APPLICATION_PAYLOAD_LEN  12U
#define UWB_TX_FRAME_LEN             (UWB_MAC_HEADER_LEN + UWB_APPLICATION_PAYLOAD_LEN)
#define UWB_MAX_SCHEDULE_INTERVAL    UINT32_C(0x7FFFFFFF)
#define UWB_MAX_INTERVAL_US          UINT32_C(8000000)
#define UWB_PERIODIC_TX_INTERVAL_US  UINT32_C(1000000)
#define UWB_TX_POLL_INTERVAL_MS      UINT32_C(1)
#define UWB_TX_TIMEOUT_MARGIN_MS     UINT32_C(100)

static const dw3000_platform_t *s_platform;
static uwb_service_state_t s_state;
static uwb_profile_t s_profile;
static uint8_t s_tx_frame[UWB_TX_FRAME_LEN];
static uint32_t s_interval_device_time;
static uint32_t s_next_transmit_time;
static uint32_t s_next_sequence;
static uint32_t s_active_sequence;
static uint32_t s_active_scheduled_time;
static uint32_t s_next_poll_time_ms;
static uint32_t s_transmit_deadline_ms;
static uwb_service_tx_event_t s_tx_event;
static bool s_tx_event_pending;

static void write_u16_le(uint8_t *destination, uint16_t value);
static void write_u32_le(uint8_t *destination, uint32_t value);
static void build_transmit_frame(
    uint32_t sequence,
    uint32_t scheduled_time);
static void publish_tx_error(
    dw3000_status_t status,
    uint32_t sequence,
    uint32_t scheduled_time);
static dw3000_status_t schedule_next_transmit(void);
static void resynchronize_transmit_schedule(void);

dw3000_status_t uwb_service_init(const dw3000_platform_t *platform)
{
  s_platform = platform;
  s_state = (uwb_service_state_t){
    .init_status = DW3000_STATUS_BAD_ARG,
    .config_status = DW3000_STATUS_NOT_READY,
    .clock_status = DW3000_STATUS_NOT_READY,
  };
  s_tx_event = (uwb_service_tx_event_t){0};
  s_tx_event_pending = false;
  s_interval_device_time = 0U;
  s_next_transmit_time = 0U;
  s_next_sequence = 0U;
  s_active_sequence = 0U;
  s_active_scheduled_time = 0U;
  s_next_poll_time_ms = 0U;
  s_transmit_deadline_ms = 0U;

  if (s_platform == NULL) {
    return s_state.init_status;
  }

  s_state.init_status = dw3000_init(&s_state.device, s_platform);
  return s_state.init_status;
}

dw3000_status_t uwb_service_run_clock_diagnostic(void)
{
  s_state.clock_diagnostic_run = true;

  if ((s_platform == NULL) || (s_state.init_status != DW3000_STATUS_OK)) {
    s_state.clock_status = DW3000_STATUS_NOT_READY;
    return s_state.clock_status;
  }

  s_state.clock_status = dw3000_run_clock_diagnostic(
      &s_state.device,
      &s_state.clock_diagnostic);
  return s_state.clock_status;
}

dw3000_status_t uwb_service_start_periodic_transmit(
    const uwb_profile_t *profile)
{
  uint32_t current_time;

  if (profile == NULL) {
    return DW3000_STATUS_BAD_ARG;
  }
  if ((UWB_PERIODIC_TX_INTERVAL_US == 0U) ||
      (UWB_PERIODIC_TX_INTERVAL_US > UWB_MAX_INTERVAL_US)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if ((s_platform == NULL) || (s_state.init_status != DW3000_STATUS_OK)) {
    return DW3000_STATUS_NOT_READY;
  }

  s_profile = *profile;
  s_state.radio_config = profile->radio;
  s_state.transmit_interval_us = UWB_PERIODIC_TX_INTERVAL_US;
  s_state.config_status = dw3000_configure_radio(
      &s_state.device,
      &profile->radio);
  if (s_state.config_status != DW3000_STATUS_OK) {
    return s_state.config_status;
  }

  s_interval_device_time = dw3000_microseconds_to_device_time(
      s_state.transmit_interval_us);
  if ((s_interval_device_time == 0U) ||
      (s_interval_device_time > UWB_MAX_SCHEDULE_INTERVAL)) {
    s_state.config_status = DW3000_STATUS_BAD_ARG;
    return s_state.config_status;
  }

  s_state.config_status = dw3000_get_system_time(
      &s_state.device,
      &current_time);
  if (s_state.config_status != DW3000_STATUS_OK) {
    return s_state.config_status;
  }

  s_state.periodic_tx_enabled = true;
  s_state.transmit_pending = false;
  s_next_sequence = 0U;
  s_next_transmit_time =
      (current_time + s_interval_device_time) & ~UINT32_C(1);
  return schedule_next_transmit();
}

void uwb_service_stop_periodic_transmit(void)
{
  if (s_state.transmit_pending) {
    (void)dw3000_abort_transmit(&s_state.device);
  }
  s_state.periodic_tx_enabled = false;
  s_state.transmit_pending = false;
}

void uwb_service_process(void)
{
  dw3000_tx_result_t result;
  dw3000_status_t status;
  uint32_t current_time_ms;

  if (!s_state.periodic_tx_enabled || s_tx_event_pending) {
    return;
  }

  if (!s_state.transmit_pending) {
    (void)schedule_next_transmit();
    return;
  }

  current_time_ms = s_platform->get_time_ms();
  if ((int32_t)(current_time_ms - s_transmit_deadline_ms) >= 0) {
    (void)dw3000_abort_transmit(&s_state.device);
    s_state.transmit_pending = false;
    publish_tx_error(
        DW3000_STATUS_TIMEOUT,
        s_active_sequence,
        s_active_scheduled_time);
    resynchronize_transmit_schedule();
    return;
  }
  if ((int32_t)(current_time_ms - s_next_poll_time_ms) < 0) {
    return;
  }
  s_next_poll_time_ms = current_time_ms + UWB_TX_POLL_INTERVAL_MS;

  status = dw3000_poll_transmit(&s_state.device, &result);
  if (status != DW3000_STATUS_OK) {
    s_state.transmit_pending = false;
    publish_tx_error(
        status,
        s_active_sequence,
        s_active_scheduled_time);
    resynchronize_transmit_schedule();
    return;
  }
  if (!result.complete) {
    return;
  }

  s_state.transmit_pending = false;
  s_state.sent_count++;
  s_tx_event = (uwb_service_tx_event_t){
    .type = UWB_SERVICE_TX_EVENT_COMPLETE,
    .status = DW3000_STATUS_OK,
    .sequence = s_active_sequence,
    .frame_len = UWB_TX_FRAME_LEN,
    .scheduled_time = s_active_scheduled_time,
    .transmit_timestamp = result.timestamp,
    .late_count = s_state.late_count,
  };
  s_tx_event_pending = true;
  s_next_transmit_time =
      (s_active_scheduled_time + s_interval_device_time) &
      ~UINT32_C(1);
}

bool uwb_service_take_tx_event(uwb_service_tx_event_t *event)
{
  if ((event == NULL) || !s_tx_event_pending) {
    return false;
  }

  *event = s_tx_event;
  s_tx_event_pending = false;
  return true;
}

const uwb_service_state_t *uwb_service_get_state(void)
{
  return &s_state;
}

static void write_u16_le(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8U);
  destination[2] = (uint8_t)(value >> 16U);
  destination[3] = (uint8_t)(value >> 24U);
}

static void build_transmit_frame(
    uint32_t sequence,
    uint32_t scheduled_time)
{
  write_u16_le(&s_tx_frame[0], UWB_FRAME_CONTROL);
  s_tx_frame[2] = (uint8_t)sequence;
  write_u16_le(&s_tx_frame[3], s_profile.pan_id);
  write_u16_le(&s_tx_frame[5], s_profile.destination_address);
  write_u16_le(&s_tx_frame[7], s_profile.source_address);

  memcpy(&s_tx_frame[9], "HWDO", 4U);
  write_u32_le(&s_tx_frame[13], sequence);
  write_u32_le(&s_tx_frame[17], scheduled_time);
}

static void publish_tx_error(
    dw3000_status_t status,
    uint32_t sequence,
    uint32_t scheduled_time)
{
  s_state.error_count++;
  if (status == DW3000_STATUS_TX_LATE) {
    s_state.late_count++;
  }

  s_tx_event = (uwb_service_tx_event_t){
    .type = UWB_SERVICE_TX_EVENT_ERROR,
    .status = status,
    .sequence = sequence,
    .frame_len = UWB_TX_FRAME_LEN,
    .scheduled_time = scheduled_time,
    .transmit_timestamp = 0U,
    .late_count = s_state.late_count,
  };
  s_tx_event_pending = true;
}

static dw3000_status_t schedule_next_transmit(void)
{
  dw3000_status_t status;
  uint32_t sequence;
  uint32_t current_time_ms;
  uint32_t interval_ms;

  if (!s_state.periodic_tx_enabled || s_state.transmit_pending) {
    return DW3000_STATUS_BUSY;
  }

  sequence = s_next_sequence++;
  build_transmit_frame(sequence, s_next_transmit_time);
  status = dw3000_transmit_delayed(
      &s_state.device,
      s_tx_frame,
      UWB_TX_FRAME_LEN,
      s_next_transmit_time);
  if (status != DW3000_STATUS_OK) {
    publish_tx_error(status, sequence, s_next_transmit_time);
    resynchronize_transmit_schedule();
    return status;
  }

  s_active_sequence = sequence;
  s_active_scheduled_time = s_next_transmit_time;
  current_time_ms = s_platform->get_time_ms();
  interval_ms =
      (s_state.transmit_interval_us + 999U) / 1000U;
  s_next_poll_time_ms = current_time_ms;
  s_transmit_deadline_ms =
      current_time_ms + interval_ms + UWB_TX_TIMEOUT_MARGIN_MS;
  s_state.transmit_pending = true;
  return DW3000_STATUS_OK;
}

static void resynchronize_transmit_schedule(void)
{
  uint32_t current_time;

  if (dw3000_get_system_time(&s_state.device, &current_time) ==
      DW3000_STATUS_OK) {
    s_next_transmit_time =
        (current_time + s_interval_device_time) & ~UINT32_C(1);
  } else {
    s_state.periodic_tx_enabled = false;
  }
}
