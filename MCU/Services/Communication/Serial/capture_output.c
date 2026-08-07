/**
  ******************************************************************************
  * @file           : capture_output.c
  * @brief          : Selectable serial encoders for UWB receive captures
  ******************************************************************************
  */

#include "capture_output.h"

#include "console_protocol.h"
#include "hcir_protocol.h"

#include <stddef.h>

static const uwb_service_cir_capture_t *s_capture;
static uwb_timestamp_result_t s_timestamp;
static serial_capture_format_t s_format;
static uint16_t s_cir_chunk_index;
static uint16_t s_cir_chunk_count;
static bool s_cir_frame_queued;

static void process_hcir(hcir_protocol_version_t version);
static void process_text_v1(void);

void capture_output_init(void)
{
  s_capture = NULL;
  s_timestamp = (uwb_timestamp_result_t){0};
  s_format = SERIAL_CAPTURE_FORMAT_OFF;
  s_cir_chunk_index = 0U;
  s_cir_chunk_count = 0U;
  s_cir_frame_queued = false;
}

void capture_output_process(void)
{
  if (s_capture == NULL) {
    return;
  }

  switch (s_format) {
    case SERIAL_CAPTURE_FORMAT_TEXT_V1:
      process_text_v1();
      break;
    case SERIAL_CAPTURE_FORMAT_HCIR_V2:
      process_hcir(HCIR_PROTOCOL_VERSION_2);
      break;
    case SERIAL_CAPTURE_FORMAT_HCIR_V3:
      process_hcir(HCIR_PROTOCOL_VERSION_3);
      break;
    default:
      s_capture = NULL;
      break;
  }
}

board_status_t capture_output_start(
    const uwb_service_cir_capture_t *capture,
    const uwb_timestamp_result_t *timestamp,
    serial_capture_format_t format)
{
  uint32_t chunk_count = 0U;

  if ((capture == NULL) || (timestamp == NULL) ||
      (capture->frame == NULL) || (capture->frame_len == 0U) ||
      (format == SERIAL_CAPTURE_FORMAT_OFF)) {
    return BOARD_BAD_ARG;
  }
  if (s_capture != NULL) {
    return BOARD_BUSY;
  }

  if ((format == SERIAL_CAPTURE_FORMAT_HCIR_V2) ||
      (format == SERIAL_CAPTURE_FORMAT_HCIR_V3)) {
    if (capture->cir_status == DW3000_STATUS_OK) {
      if ((capture->cir_data == NULL) ||
          (capture->cir_sample_count == 0U) ||
          (capture->cir_sample_bytes == 0U)) {
        return BOARD_BAD_ARG;
      }
      chunk_count =
          ((uint32_t)capture->cir_sample_count +
           HCIR_SAMPLES_PER_CHUNK - 1U) /
          HCIR_SAMPLES_PER_CHUNK;
      if ((chunk_count == 0U) || (chunk_count > UINT16_MAX)) {
        return BOARD_BAD_ARG;
      }
    }
  } else if (format != SERIAL_CAPTURE_FORMAT_TEXT_V1) {
    return BOARD_BAD_ARG;
  }

  s_capture = capture;
  s_timestamp = *timestamp;
  s_format = format;
  s_cir_chunk_index = 0U;
  s_cir_chunk_count = (uint16_t)chunk_count;
  s_cir_frame_queued = false;
  return BOARD_OK;
}

bool capture_output_busy(void)
{
  return s_capture != NULL;
}

bool capture_output_requires_cir(serial_capture_format_t format)
{
  return (format == SERIAL_CAPTURE_FORMAT_HCIR_V2) ||
         (format == SERIAL_CAPTURE_FORMAT_HCIR_V3);
}

const char *capture_output_format_name(serial_capture_format_t format)
{
  switch (format) {
    case SERIAL_CAPTURE_FORMAT_OFF:
      return "OFF";
    case SERIAL_CAPTURE_FORMAT_TEXT_V1:
      return "TEXT_V1";
    case SERIAL_CAPTURE_FORMAT_HCIR_V2:
      return "HCIR_V2";
    case SERIAL_CAPTURE_FORMAT_HCIR_V3:
      return "HCIR_V3";
    default:
      return "INVALID";
  }
}

bool capture_output_format_is_supported(serial_capture_format_t format)
{
  return (format == SERIAL_CAPTURE_FORMAT_OFF) ||
      (format == SERIAL_CAPTURE_FORMAT_TEXT_V1) ||
      (format == SERIAL_CAPTURE_FORMAT_HCIR_V2) ||
      (format == SERIAL_CAPTURE_FORMAT_HCIR_V3);
}

static void process_hcir(hcir_protocol_version_t version)
{
  board_status_t status;
  uint32_t relative_offset;
  uint16_t remaining;
  uint16_t sample_count;

  if (!s_cir_frame_queued) {
    status = hcir_protocol_send_frame(s_capture, version);
    if (status == BOARD_BUSY) {
      return;
    }
    if (status != BOARD_OK) {
      s_capture = NULL;
      return;
    }
    s_cir_frame_queued = true;
  }

  if (s_cir_chunk_index >= s_cir_chunk_count) {
    s_capture = NULL;
    return;
  }

  relative_offset =
      (uint32_t)s_cir_chunk_index * HCIR_SAMPLES_PER_CHUNK;
  remaining = s_capture->cir_sample_count - (uint16_t)relative_offset;
  sample_count = (remaining > HCIR_SAMPLES_PER_CHUNK)
      ? HCIR_SAMPLES_PER_CHUNK
      : remaining;
  status = hcir_protocol_send_samples(
      s_capture,
      version,
      s_cir_chunk_index,
      s_cir_chunk_count,
      (uint16_t)relative_offset,
      sample_count);
  if (status == BOARD_BUSY) {
    return;
  }
  if (status != BOARD_OK) {
    s_capture = NULL;
    return;
  }

  s_cir_chunk_index++;
  if (s_cir_chunk_index >= s_cir_chunk_count) {
    s_capture = NULL;
  }
}

static void process_text_v1(void)
{
  uint8_t storage[448];
  console_protocol_message_t message;
  board_status_t status;

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(&message, "UWB RX V1, CAPTURE=0x");
  (void)console_protocol_append_hex(&message, s_capture->capture_id, 8U);
  (void)console_protocol_append_text(&message, ", MCU_MS=0x");
  (void)console_protocol_append_hex(
      &message,
      s_capture->mcu_system_time_ms,
      8U);
  (void)console_protocol_append_text(&message, ", REF_VALID=");
  (void)console_protocol_append_bool(
      &message,
      s_capture->reference_time_valid);
  (void)console_protocol_append_text(&message, ", REF_MS=0x");
  (void)console_protocol_append_hex(
      &message,
      s_capture->reference_time_ms,
      8U);
  (void)console_protocol_append_text(&message, ", RX_TS=0x");
  (void)console_protocol_append_hex64(
      &message,
      s_capture->receive_timestamp,
      10U);
  (void)console_protocol_append_text(&message, ", RAW_TS=0x");
  (void)console_protocol_append_hex64(
      &message,
      s_capture->raw_receive_timestamp,
      10U);
  (void)console_protocol_append_text(&message, ", SELECTED_TS=");
  if (s_timestamp.status == UWB_TIMESTAMP_RESULT_OK) {
    (void)console_protocol_append_text(&message, "0x");
    (void)console_protocol_append_hex64(
        &message,
        s_timestamp.timestamp_dtu,
        10U);
  } else {
    (void)console_protocol_append_text(&message, "PENDING");
  }
  (void)console_protocol_append_text(&message, ", TS_SOURCE=");
  (void)console_protocol_append_text(
      &message,
      uwb_timestamp_estimator_name(s_timestamp.estimator));
  (void)console_protocol_append_text(&message, ", FPI=0x");
  (void)console_protocol_append_hex(
      &message,
      s_capture->diagnostic.first_path_index,
      4U);
  (void)console_protocol_append_text(&message, ", PEAK=0x");
  (void)console_protocol_append_hex(
      &message,
      s_capture->diagnostic.peak_index,
      4U);
  (void)console_protocol_append_text(&message, ", RSSI_Q8_8=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint16_t)s_capture->diagnostic.rssi_q8_8,
      4U);
  (void)console_protocol_append_text(&message, ", FP_PWR_Q8_8=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint16_t)s_capture->diagnostic.first_path_power_q8_8,
      4U);
  (void)console_protocol_append_text(&message, ", RF_PORT=");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)s_capture->rf_port,
      1U);

  status = console_protocol_send_with_crc(&message);
  if (status == BOARD_BUSY) {
    return;
  }
  s_capture = NULL;
}
