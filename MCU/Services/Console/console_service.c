/**
  ******************************************************************************
  * @file           : console_service.c
  * @brief          : PC console reporting service
  ******************************************************************************
  */

#include "console_service.h"

#include "cir_protocol.h"
#include "console_protocol.h"

static const uwb_service_cir_capture_t *s_cir_export;
static uint16_t s_cir_chunk_index;
static uint16_t s_cir_chunk_count;
static bool s_cir_frame_queued;

static void console_service_process_cir_export(void);

void console_service_init(void)
{
  s_cir_export = NULL;
  s_cir_chunk_index = 0U;
  s_cir_chunk_count = 0U;
  s_cir_frame_queued = false;
  (void)console_protocol_write("\r\n=== HopWINS-UWBDO-MCU ===\r\n");
}

void console_service_process(void)
{
  console_service_process_cir_export();
  board_pc_tx_process();
}

void console_service_write(const char *text)
{
  (void)console_protocol_write(text);
}

void console_service_report_firmware_profile(
    const board_capabilities_t *board,
    const char *role_name,
    const char *build_type)
{
  uint8_t storage[192];
  console_protocol_message_t message;

  if ((board == NULL) || (role_name == NULL) || (build_type == NULL)) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(&message, "FW PROFILE, BOARD=");
  (void)console_protocol_append_text(&message, board->name);
  (void)console_protocol_append_text(&message, ", ROLE=");
  (void)console_protocol_append_text(&message, role_name);
  (void)console_protocol_append_text(&message, ", BUILD=");
  (void)console_protocol_append_text(&message, build_type);
  (void)console_protocol_append_text(&message, ", FPGA=");
  (void)console_protocol_append_bool(&message, board->has_fpga);
  (void)console_protocol_append_text(&message, ", CLOCK=");
  (void)console_protocol_append_bool(
      &message,
      board->has_clock_control);
  (void)console_protocol_append_text(&message, ", EXT_TIMER=");
  (void)console_protocol_append_bool(
      &message,
      board->has_external_clock_counter);
  (void)console_protocol_send_with_crc(&message);
}

void console_service_report_fpga_image(const fpga_service_state_t *state)
{
  uint8_t storage[128];
  console_protocol_message_t message;

  if (state == NULL) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(&message, "FPGA IMAGE, IMG_STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->image.status,
      2U);
  (void)console_protocol_append_text(&message, ", IMG_LEN=0x");
  (void)console_protocol_append_hex(&message, state->image.image.len, 8U);
  (void)console_protocol_append_text(&message, ", SYNC=0x");
  (void)console_protocol_append_hex(&message, state->image.sync_offset, 8U);
  (void)console_protocol_send_with_crc(&message);
}

void console_service_report_fpga_result(const fpga_service_state_t *state)
{
  uint8_t storage[128];
  console_protocol_message_t message;

  if (state == NULL) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(
      &message,
      (state->configure_status == ICE40_STATUS_OK)
          ? "FPGA CONFIG OK"
          : "FPGA CONFIG ERROR");
  (void)console_protocol_append_text(&message, ", STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->configure_status,
      2U);
  (void)console_protocol_append_text(&message, ", ATTEMPTS=");
  (void)console_protocol_append_hex(&message, state->device.attempts, 1U);
  (void)console_protocol_append_text(&message, ", CDONE_AT_RST=");
  (void)console_protocol_append_bool(&message, state->device.cdone_at_reset);
  (void)console_protocol_append_text(&message, ", CDONE_CLK=0x");
  (void)console_protocol_append_hex(&message, state->device.cdone_clocks, 8U);
  (void)console_protocol_append_text(&message, ", CDONE=");
  (void)console_protocol_append_bool(&message, state->cdone_pin);
  (void)console_protocol_send_with_crc(&message);
}

void console_service_report_uwb(const uwb_service_state_t *state)
{
  uint8_t storage[224];
  console_protocol_message_t message;

  if (state == NULL) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(
      &message,
      (state->init_status == DW3000_STATUS_OK)
          ? "DW3000 INIT OK"
          : "DW3000 INIT ERROR");
  (void)console_protocol_append_text(&message, ", STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->init_status,
      2U);
  (void)console_protocol_append_text(&message, ", DEV_ID=0x");
  (void)console_protocol_append_hex(&message, state->device.device_id, 8U);

  if (!state->clock_diagnostic_run) {
    (void)console_protocol_append_text(&message, ", CLOCK=NOT_RUN");
    (void)console_protocol_send_with_crc(&message);
    return;
  }

  (void)console_protocol_append_text(
      &message,
      (state->clock_status == DW3000_STATUS_OK)
          ? ", CLOCK=OK"
          : ", CLOCK=ERROR");
  (void)console_protocol_append_text(&message, ", CLK_STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->clock_status,
      2U);
  (void)console_protocol_append_text(&message, ", PLL_RC=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint32_t)state->clock_diagnostic.pll_result,
      8U);
  (void)console_protocol_append_text(&message, ", RESTORE_RC=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint32_t)state->clock_diagnostic.restore_result,
      8U);
  (void)console_protocol_append_text(&message, ", PLL_STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      state->clock_diagnostic.pll_status,
      8U);
  (void)console_protocol_append_text(&message, ", XTAL=");
  (void)console_protocol_append_bool(
      &message,
      state->clock_diagnostic.xtal_settled);
  (void)console_protocol_append_text(&message, ", LOCK=");
  (void)console_protocol_append_bool(
      &message,
      state->clock_diagnostic.pll_locked);
  (void)console_protocol_append_text(&message, ", CAL=");
  (void)console_protocol_append_bool(
      &message,
      state->clock_diagnostic.calibration_done);
  (void)console_protocol_send_with_crc(&message);
}

void console_service_report_uwb_config(const uwb_service_state_t *state)
{
  uint8_t storage[256];
  console_protocol_message_t message;

  if (state == NULL) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(
      &message,
      (state->config_status == DW3000_STATUS_OK)
          ? "UWB CFG OK"
          : "UWB CFG ERROR");
  (void)console_protocol_append_text(&message, ", STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->config_status,
      2U);
  (void)console_protocol_append_text(&message, ", CH=");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->radio_config.channel,
      1U);
  (void)console_protocol_append_text(
      &message,
      (state->radio_config.data_rate == DW3000_DATA_RATE_6M8)
          ? ", RATE=6M8"
          : ", RATE=850K");
  (void)console_protocol_append_text(&message, ", PREAMBLE=0x");
  (void)console_protocol_append_hex(
      &message,
      state->radio_config.preamble_length,
      4U);
  (void)console_protocol_append_text(&message, ", CODE=0x");
  (void)console_protocol_append_hex(
      &message,
      state->radio_config.tx_preamble_code,
      2U);
  if (state->mode == UWB_SERVICE_MODE_PERIODIC_TX) {
    (void)console_protocol_append_text(&message, ", MODE=TX, PERIOD_US=0x");
    (void)console_protocol_append_hex(
        &message,
        state->transmit_interval_us,
        8U);
  } else if (state->mode == UWB_SERVICE_MODE_RX_CIR) {
    (void)console_protocol_append_text(&message, ", MODE=RX_CIR, CIR=0x");
    (void)console_protocol_append_hex(
        &message,
        state->cir_capture_samples,
        4U);
    (void)console_protocol_append_text(&message, "/0x");
    (void)console_protocol_append_hex(
        &message,
        state->cir_total_samples,
        4U);
  } else {
    (void)console_protocol_append_text(&message, ", MODE=IDLE");
  }
  (void)console_protocol_append_text(&message, ", TX_POWER=0x");
  (void)console_protocol_append_hex(
      &message,
      state->radio_config.tx_power,
      8U);
  (void)console_protocol_append_text(&message, ", TX_ANTD=0x");
  (void)console_protocol_append_hex(
      &message,
      state->radio_config.tx_antenna_delay,
      4U);
  (void)console_protocol_append_text(&message, ", RX_ANTD=0x");
  (void)console_protocol_append_hex(
      &message,
      state->radio_config.rx_antenna_delay,
      4U);
  (void)console_protocol_append_text(&message, ", RF_PORT=");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->radio_config.rf_port,
      1U);
  (void)console_protocol_send_with_crc(&message);
}

void console_service_report_uwb_rx_health(
    const uwb_service_state_t *state)
{
  uint8_t storage[256];
  console_protocol_message_t message;

  if (state == NULL) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(&message, "UWB RX HEALTH, ENABLE=");
  (void)console_protocol_append_bool(
      &message,
      state->cir_receive_enabled);
  (void)console_protocol_append_text(&message, ", PENDING=");
  (void)console_protocol_append_bool(&message, state->receive_pending);
  (void)console_protocol_append_text(&message, ", QUEUED=0x");
  (void)console_protocol_append_hex(
      &message,
      state->queued_capture_count,
      2U);
  (void)console_protocol_append_text(&message, ", RX=0x");
  (void)console_protocol_append_hex(
      &message,
      state->received_count,
      8U);
  (void)console_protocol_append_text(&message, ", ERR=0x");
  (void)console_protocol_append_hex(
      &message,
      state->receive_error_count,
      8U);
  (void)console_protocol_append_text(&message, ", CRC_ERR=0x");
  (void)console_protocol_append_hex(
      &message,
      state->receive_crc_error_count,
      8U);
  (void)console_protocol_append_text(&message, ", RECOVERY=0x");
  (void)console_protocol_append_hex(
      &message,
      state->receive_recovery_count,
      8U);
  (void)console_protocol_append_text(&message, ", WATCHDOG=0x");
  (void)console_protocol_append_hex(
      &message,
      state->receive_watchdog_count,
      8U);
  (void)console_protocol_append_text(&message, ", QFULL=0x");
  (void)console_protocol_append_hex(
      &message,
      state->capture_queue_full_count,
      8U);
  (void)console_protocol_append_text(&message, ", UART_ERR=0x");
  (void)console_protocol_append_hex(
      &message,
      board_pc_tx_error_count(),
      8U);
  (void)console_protocol_send_with_crc(&message);
}

void console_service_report_uwb_tx(
    const uwb_service_tx_event_t *event)
{
  uint8_t storage[192];
  console_protocol_message_t message;

  if ((event == NULL) || (event->type == UWB_SERVICE_TX_EVENT_NONE)) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(
      &message,
      (event->type == UWB_SERVICE_TX_EVENT_COMPLETE)
          ? "UWB TX OK"
          : "UWB TX ERROR");
  (void)console_protocol_append_text(&message, ", STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)event->status,
      2U);
  (void)console_protocol_append_text(&message, ", SEQ=0x");
  (void)console_protocol_append_hex(&message, event->sequence, 8U);
  (void)console_protocol_append_text(&message, ", LEN=0x");
  (void)console_protocol_append_hex(&message, event->frame_len, 4U);
  (void)console_protocol_append_text(&message, ", SCHED=0x");
  (void)console_protocol_append_hex(
      &message,
      event->scheduled_time,
      8U);
  (void)console_protocol_append_text(&message, ", TX_TS=0x");
  (void)console_protocol_append_hex64(
      &message,
      event->transmit_timestamp,
      10U);
  (void)console_protocol_append_text(&message, ", LATE=0x");
  (void)console_protocol_append_hex(&message, event->late_count, 8U);
  (void)console_protocol_send_with_crc(&message);
}

board_status_t console_service_start_uwb_cir_export(
    const uwb_service_cir_capture_t *capture)
{
  uint32_t chunk_count;

  if ((capture == NULL) || (capture->frame == NULL) ||
      (capture->frame_len == 0U)) {
    return BOARD_BAD_ARG;
  }
  if (s_cir_export != NULL) {
    return BOARD_BUSY;
  }

  chunk_count = 0U;
  if (capture->cir_status == DW3000_STATUS_OK) {
    if ((capture->cir_data == NULL) ||
        (capture->cir_sample_count == 0U) ||
        (capture->cir_sample_bytes == 0U)) {
      return BOARD_BAD_ARG;
    }
    chunk_count =
        ((uint32_t)capture->cir_sample_count +
         CIR_PROTOCOL_SAMPLES_PER_CHUNK - 1U) /
        CIR_PROTOCOL_SAMPLES_PER_CHUNK;
    if ((chunk_count == 0U) || (chunk_count > UINT16_MAX)) {
      return BOARD_BAD_ARG;
    }
  }

  s_cir_export = capture;
  s_cir_chunk_index = 0U;
  s_cir_chunk_count = (uint16_t)chunk_count;
  s_cir_frame_queued = false;
  return BOARD_OK;
}

bool console_service_uwb_cir_export_busy(void)
{
  return s_cir_export != NULL;
}

static void console_service_process_cir_export(void)
{
  board_status_t status;
  uint32_t relative_offset;
  uint16_t remaining;
  uint16_t sample_count;

  if (s_cir_export == NULL) {
    return;
  }

  if (!s_cir_frame_queued) {
    status = cir_protocol_send_frame(s_cir_export);
    if (status == BOARD_BUSY) {
      return;
    }
    if (status != BOARD_OK) {
      s_cir_export = NULL;
      return;
    }
    s_cir_frame_queued = true;
  }

  if (s_cir_chunk_index >= s_cir_chunk_count) {
    s_cir_export = NULL;
    return;
  }

  relative_offset =
      (uint32_t)s_cir_chunk_index * CIR_PROTOCOL_SAMPLES_PER_CHUNK;
  remaining =
      s_cir_export->cir_sample_count - (uint16_t)relative_offset;
  sample_count = (remaining > CIR_PROTOCOL_SAMPLES_PER_CHUNK)
      ? CIR_PROTOCOL_SAMPLES_PER_CHUNK
      : remaining;
  status = cir_protocol_send_samples(
      s_cir_export,
      s_cir_chunk_index,
      s_cir_chunk_count,
      (uint16_t)relative_offset,
      sample_count);
  if (status == BOARD_BUSY) {
    return;
  }
  if (status != BOARD_OK) {
    s_cir_export = NULL;
    return;
  }

  s_cir_chunk_index++;
  if (s_cir_chunk_index >= s_cir_chunk_count) {
    s_cir_export = NULL;
  }
}
