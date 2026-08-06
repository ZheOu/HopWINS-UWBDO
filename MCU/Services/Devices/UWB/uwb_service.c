/**
  ******************************************************************************
  * @file           : uwb_service.c
  * @brief          : DW3000 initialization, TX, RX, and CIR service
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
#define UWB_PERIODIC_TX_INTERVAL_US  UINT32_C(100000)
#define UWB_TX_POLL_INTERVAL_MS      UINT32_C(1)
#define UWB_TX_TIMEOUT_MARGIN_MS     UINT32_C(100)
#define UWB_RX_POLL_INTERVAL_MS      UINT32_C(1)
#define UWB_RX_RESTART_INTERVAL_MS   UINT32_C(20)
#define UWB_RX_NO_FRAME_TIMEOUT_MS   UINT32_C(5000)
#define UWB_RX_MAX_START_FAILURES    8U
#define UWB_RX_MAX_WATCHDOG_RESTARTS 3U
#define UWB_RX_CAPTURE_SLOT_COUNT    2U

static const dw3000_platform_t *s_device_platform;
static const uwb_service_time_source_t *s_time_source;
static uwb_service_state_t s_state;
static uwb_profile_t s_profile;
static uwb_service_cir_config_t s_cir_config;
static uint8_t s_tx_frame[UWB_TX_FRAME_LEN];
typedef struct {
  uint8_t frame[DW3000_RX_FRAME_MAX_LEN];
  uint8_t cir_data[DW3000_CIR_MAX_SAMPLES * DW3000_CIR_SAMPLE_BYTES];
  uwb_service_cir_capture_t capture;
} uwb_service_capture_slot_t;

static uwb_service_capture_slot_t
    s_capture_slots[UWB_RX_CAPTURE_SLOT_COUNT];
static uint32_t s_interval_device_time;
static uint32_t s_next_transmit_time;
static uint32_t s_next_sequence;
static uint32_t s_active_sequence;
static uint32_t s_active_scheduled_time;
static uint32_t s_next_poll_time_ms;
static uint32_t s_transmit_deadline_ms;
static uint32_t s_receive_started_time_ms;
static uint32_t s_next_receive_restart_time_ms;
static uwb_service_tx_event_t s_tx_event;
static bool s_tx_event_pending;
static uint32_t s_next_capture_id;
static uint8_t s_capture_read_index;
static uint8_t s_capture_write_index;
static uint8_t s_capture_count;
static uint8_t s_receive_start_failure_count;
static uint8_t s_watchdog_restarts_without_frame;

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
static void process_periodic_transmit(void);
static void process_cir_receive(void);
static dw3000_status_t restart_cir_receive(bool recovery);
static void reset_capture_queue(void);
static dw3000_rf_port_t rf_port_from_mode(dw3000_rf_mode_t mode);
static uint16_t select_cir_sample_offset(
    uint16_t total_samples,
    uint16_t capture_samples,
    uint16_t first_path_index_q10_6);

dw3000_status_t uwb_service_init(
    const dw3000_platform_t *device_platform,
    const uwb_service_time_source_t *time_source)
{
  s_device_platform = device_platform;
  s_time_source = time_source;
  s_state = (uwb_service_state_t){
    .init_status = DW3000_STATUS_BAD_ARG,
    .config_status = DW3000_STATUS_NOT_READY,
  };
  s_tx_event = (uwb_service_tx_event_t){0};
  s_tx_event_pending = false;
  reset_capture_queue();
  s_next_capture_id = 0U;
  s_interval_device_time = 0U;
  s_next_transmit_time = 0U;
  s_next_sequence = 0U;
  s_active_sequence = 0U;
  s_active_scheduled_time = 0U;
  s_next_poll_time_ms = 0U;
  s_transmit_deadline_ms = 0U;
  s_receive_started_time_ms = 0U;
  s_next_receive_restart_time_ms = 0U;
  s_receive_start_failure_count = 0U;
  s_watchdog_restarts_without_frame = 0U;
  s_cir_config = (uwb_service_cir_config_t){0};

  if ((s_device_platform == NULL) || (s_time_source == NULL) ||
      (s_time_source->get_monotonic_time_ms == NULL)) {
    return s_state.init_status;
  }

  s_state.init_status = dw3000_init(&s_state.device, s_device_platform);
  return s_state.init_status;
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
  if ((s_device_platform == NULL) ||
      (s_state.init_status != DW3000_STATUS_OK)) {
    return DW3000_STATUS_NOT_READY;
  }
  if (s_state.cir_receive_enabled) {
    return DW3000_STATUS_BUSY;
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
  s_state.mode = UWB_SERVICE_MODE_PERIODIC_TX;
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
  if (s_state.mode == UWB_SERVICE_MODE_PERIODIC_TX) {
    s_state.mode = UWB_SERVICE_MODE_IDLE;
  }
}

dw3000_status_t uwb_service_start_cir_receive(
    const uwb_profile_t *profile,
    const uwb_service_cir_config_t *config)
{
  uint16_t total_samples;
  uint32_t requested_samples;
  dw3000_status_t status;

  if ((profile == NULL) || (config == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if ((s_device_platform == NULL) ||
      (s_state.init_status != DW3000_STATUS_OK)) {
    return DW3000_STATUS_NOT_READY;
  }
  if (s_state.periodic_tx_enabled) {
    return DW3000_STATUS_BUSY;
  }

  s_profile = *profile;
  s_cir_config = *config;
  requested_samples = s_cir_config.sample_count;
  s_state.radio_config = profile->radio;
  s_state.config_status = dw3000_configure_radio(
      &s_state.device,
      &profile->radio);
  if (s_state.config_status != DW3000_STATUS_OK) {
    return s_state.config_status;
  }

  total_samples = 0U;
  if (s_cir_config.capture_cir) {
    total_samples = dw3000_get_cir_sample_count(&s_state.device);
    if (total_samples == 0U) {
      s_state.config_status = DW3000_STATUS_CIR_ERROR;
      return s_state.config_status;
    }
    if ((requested_samples == 0U) ||
        (requested_samples > total_samples)) {
      requested_samples = total_samples;
    }
  } else {
    requested_samples = 0U;
  }

  s_state.mode = UWB_SERVICE_MODE_CONTINUOUS_RX;
  s_state.cir_receive_enabled = true;
  s_state.cir_capture_enabled = s_cir_config.capture_cir;
  s_state.receive_pending = false;
  s_state.cir_capture_ready = false;
  s_state.cir_total_samples = total_samples;
  s_state.cir_capture_samples = (uint16_t)requested_samples;
  reset_capture_queue();
  s_next_capture_id = 0U;
  s_receive_start_failure_count = 0U;
  s_watchdog_restarts_without_frame = 0U;

  status = restart_cir_receive(false);
  if (status != DW3000_STATUS_OK) {
    s_state.cir_receive_enabled = false;
    s_state.mode = UWB_SERVICE_MODE_IDLE;
    s_state.config_status = status;
    return status;
  }

  return DW3000_STATUS_OK;
}

void uwb_service_stop_cir_receive(void)
{
  if (s_state.receive_pending || s_state.cir_capture_ready) {
    (void)dw3000_abort_receive(&s_state.device);
  }

  s_state.cir_receive_enabled = false;
  s_state.cir_capture_enabled = false;
  s_state.receive_pending = false;
  s_state.cir_capture_ready = false;
  reset_capture_queue();
  if (s_state.mode == UWB_SERVICE_MODE_CONTINUOUS_RX) {
    s_state.mode = UWB_SERVICE_MODE_IDLE;
  }
}

const uwb_service_cir_capture_t *uwb_service_get_cir_capture(void)
{
  return (s_capture_count != 0U)
             ? &s_capture_slots[s_capture_read_index].capture
             : NULL;
}

dw3000_status_t uwb_service_release_cir_capture(void)
{
  if (s_capture_count == 0U) {
    return DW3000_STATUS_NOT_READY;
  }

  s_capture_slots[s_capture_read_index].capture =
      (uwb_service_cir_capture_t){0};
  s_capture_read_index =
      (uint8_t)((s_capture_read_index + 1U) % UWB_RX_CAPTURE_SLOT_COUNT);
  s_capture_count--;
  s_state.queued_capture_count = s_capture_count;
  s_state.cir_capture_ready = s_capture_count != 0U;

  if (s_state.cir_receive_enabled && !s_state.receive_pending) {
    return restart_cir_receive(false);
  }
  return DW3000_STATUS_OK;
}

void uwb_service_process(void)
{
  if (s_state.cir_receive_enabled) {
    process_cir_receive();
    return;
  }

  process_periodic_transmit();
}

static void process_periodic_transmit(void)
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

  current_time_ms = s_time_source->get_monotonic_time_ms();
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

static void process_cir_receive(void)
{
  uwb_service_capture_slot_t *slot;
  dw3000_rx_result_t result;
  dw3000_cir_diagnostic_t diagnostic = {0};
  dw3000_rx_register_snapshot_t register_snapshot = {0};
  dw3000_status_t status;
  dw3000_status_t diagnostic_status;
  dw3000_status_t cir_status;
  dw3000_status_t register_status;
  uint32_t current_time_ms;
  uint32_t reference_time_ms = 0U;
  uint16_t sample_offset;
  uint16_t sample_count;
  bool reference_time_valid = false;

  current_time_ms = s_time_source->get_monotonic_time_ms();
  if (!s_state.receive_pending) {
    if ((s_capture_count < UWB_RX_CAPTURE_SLOT_COUNT) &&
        ((int32_t)(current_time_ms - s_next_receive_restart_time_ms) >= 0)) {
      (void)restart_cir_receive(true);
    }
    return;
  }

  if ((uint32_t)(current_time_ms - s_receive_started_time_ms) >=
      UWB_RX_NO_FRAME_TIMEOUT_MS) {
    (void)dw3000_abort_receive(&s_state.device);
    s_state.receive_pending = false;
    s_state.receive_watchdog_count++;
    s_watchdog_restarts_without_frame++;
    if (s_watchdog_restarts_without_frame >=
        UWB_RX_MAX_WATCHDOG_RESTARTS) {
      s_state.cir_receive_enabled = false;
      s_state.mode = UWB_SERVICE_MODE_IDLE;
      s_state.config_status = DW3000_STATUS_TIMEOUT;
      return;
    }
    s_next_receive_restart_time_ms = current_time_ms;
    (void)restart_cir_receive(true);
    return;
  }

  if ((int32_t)(current_time_ms - s_next_poll_time_ms) < 0) {
    return;
  }
  s_next_poll_time_ms = current_time_ms + UWB_RX_POLL_INTERVAL_MS;
  slot = &s_capture_slots[s_capture_write_index];

  status = dw3000_poll_receive(
      &s_state.device,
      slot->frame,
      (uint16_t)sizeof(slot->frame),
      &result);
  if (status != DW3000_STATUS_OK) {
    s_state.receive_pending = false;
    s_state.receive_error_count++;
    if (status == DW3000_STATUS_RX_CRC_ERROR) {
      s_state.receive_crc_error_count++;
    }
    s_next_receive_restart_time_ms =
        current_time_ms + UWB_RX_RESTART_INTERVAL_MS;
    return;
  }
  if (!result.complete) {
    return;
  }

  s_state.receive_pending = false;
  s_watchdog_restarts_without_frame = 0U;
  if (s_time_source->get_reference_time_ms != NULL) {
    reference_time_valid =
        s_time_source->get_reference_time_ms(&reference_time_ms);
  }

  register_status = DW3000_STATUS_NOT_READY;
  diagnostic_status = DW3000_STATUS_NOT_READY;
  cir_status = DW3000_STATUS_NOT_READY;
  sample_offset = 0U;
  sample_count = 0U;

  if (s_state.cir_capture_enabled) {
    register_status = dw3000_read_rx_register_snapshot(
        &s_state.device,
        &register_snapshot);
    diagnostic_status = dw3000_read_cir_diagnostics(
        &s_state.device,
        &diagnostic);
    cir_status = diagnostic_status;
    if (diagnostic_status == DW3000_STATUS_OK) {
      sample_count = s_state.cir_capture_samples;
      sample_offset = select_cir_sample_offset(
          s_state.cir_total_samples,
          sample_count,
          diagnostic.first_path_index);
      cir_status = dw3000_read_cir_48b(
          &s_state.device,
          sample_offset,
          sample_count,
          slot->cir_data);
    }
    if ((register_status != DW3000_STATUS_OK) ||
        (diagnostic_status != DW3000_STATUS_OK) ||
        (cir_status != DW3000_STATUS_OK)) {
      s_state.receive_error_count++;
    }
  }

  slot->capture = (uwb_service_cir_capture_t){
    .capture_id = s_next_capture_id++,
    .frame = slot->frame,
    .frame_len = result.frame_len,
    .mcu_system_time_ms = current_time_ms,
    .reference_time_ms = reference_time_ms,
    .reference_time_valid = reference_time_valid,
    .receive_timestamp = result.timestamp,
    .raw_receive_timestamp = result.raw_timestamp,
    .system_status = result.system_status,
    .register_snapshot = register_snapshot,
    .register_status = register_status,
    .clock_offset = result.clock_offset,
    .carrier_integrator = result.carrier_integrator,
    .ranging_frame = result.ranging_frame,
    .diagnostic = diagnostic,
    .diagnostic_status = diagnostic_status,
    .cir_data =
        (cir_status == DW3000_STATUS_OK) ? slot->cir_data : NULL,
    .cir_data_len =
        (cir_status == DW3000_STATUS_OK)
            ? (uint32_t)sample_count * DW3000_CIR_SAMPLE_BYTES
            : 0U,
    .cir_sample_offset = sample_offset,
    .cir_sample_count =
        (cir_status == DW3000_STATUS_OK) ? sample_count : 0U,
    .cir_sample_bytes =
        (cir_status == DW3000_STATUS_OK)
            ? DW3000_CIR_SAMPLE_BYTES
            : 0U,
    .cir_status = cir_status,
    .rf_port = rf_port_from_mode(s_state.radio_config.rf_mode),
    .rx_antenna_delay = s_state.radio_config.rx_antenna_delay,
  };
  s_capture_write_index =
      (uint8_t)((s_capture_write_index + 1U) % UWB_RX_CAPTURE_SLOT_COUNT);
  s_capture_count++;
  s_state.received_count++;
  s_state.queued_capture_count = s_capture_count;
  s_state.cir_capture_ready = s_capture_count != 0U;

  if (s_capture_count < UWB_RX_CAPTURE_SLOT_COUNT) {
    (void)restart_cir_receive(false);
  } else {
    s_state.capture_queue_full_count++;
  }
}

static dw3000_status_t restart_cir_receive(bool recovery)
{
  uint32_t current_time_ms = s_time_source->get_monotonic_time_ms();
  dw3000_status_t status;

  if (!s_state.cir_receive_enabled ||
      (s_capture_count >= UWB_RX_CAPTURE_SLOT_COUNT)) {
    return DW3000_STATUS_BUSY;
  }
  if (recovery) {
    s_state.receive_recovery_count++;
  }

  status = dw3000_receive_start(&s_state.device);
  if (status == DW3000_STATUS_BUSY) {
    (void)dw3000_abort_receive(&s_state.device);
    status = dw3000_receive_start(&s_state.device);
  }

  if (status == DW3000_STATUS_OK) {
    s_state.receive_pending = true;
    s_state.config_status = DW3000_STATUS_OK;
    s_receive_started_time_ms = current_time_ms;
    s_next_poll_time_ms = current_time_ms;
    s_receive_start_failure_count = 0U;
    return status;
  }

  s_state.receive_pending = false;
  s_state.receive_error_count++;
  s_state.config_status = status;
  s_receive_start_failure_count++;
  s_next_receive_restart_time_ms =
      current_time_ms + UWB_RX_RESTART_INTERVAL_MS;
  if (s_receive_start_failure_count >= UWB_RX_MAX_START_FAILURES) {
    s_state.cir_receive_enabled = false;
    s_state.mode = UWB_SERVICE_MODE_IDLE;
  }
  return status;
}

static dw3000_rf_port_t rf_port_from_mode(dw3000_rf_mode_t mode)
{
  if (mode == DW3000_RF_MODE_MANUAL_1) {
    return DW3000_RF_PORT_1;
  }
  if (mode == DW3000_RF_MODE_MANUAL_2) {
    return DW3000_RF_PORT_2;
  }
  return DW3000_RF_PORT_NONE;
}

static void reset_capture_queue(void)
{
  memset(s_capture_slots, 0, sizeof(s_capture_slots));
  s_capture_read_index = 0U;
  s_capture_write_index = 0U;
  s_capture_count = 0U;
  s_state.queued_capture_count = 0U;
  s_state.cir_capture_ready = false;
}

static uint16_t select_cir_sample_offset(
    uint16_t total_samples,
    uint16_t capture_samples,
    uint16_t first_path_index_q10_6)
{
  uint32_t first_path_sample = first_path_index_q10_6 >> 6U;
  uint32_t pre_samples = s_cir_config.pre_first_path_samples;
  uint32_t offset;

  if (capture_samples >= total_samples) {
    return 0U;
  }

  offset = (first_path_sample > pre_samples)
      ? (first_path_sample - pre_samples)
      : 0U;
  if ((offset + capture_samples) > total_samples) {
    offset = total_samples - capture_samples;
  }

  return (uint16_t)offset;
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
  current_time_ms = s_time_source->get_monotonic_time_ms();
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
