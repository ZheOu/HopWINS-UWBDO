/**
  ******************************************************************************
  * @file           : console_service.c
  * @brief          : PC console reporting service
  ******************************************************************************
  */

#include "console_service.h"

#include "capture_output.h"
#include "console_protocol.h"

static bool s_boot_mode;

static board_status_t console_service_send(
    console_protocol_message_t *message);
static const char *uwb_rf_mode_name(dw3000_rf_mode_t mode);

void console_service_init(void)
{
  capture_output_init();
  s_boot_mode = true;
  (void)console_protocol_write_blocking(
      "\r\n=== HopWINS-UWBDO-MCU ===\r\n");
}

void console_service_finish_boot(void)
{
  s_boot_mode = false;
}

void console_service_process(void)
{
  capture_output_process();
  board_pc_tx_process();
}

void console_service_write(const char *text)
{
  if (s_boot_mode) {
    (void)console_protocol_write_blocking(text);
  } else {
    (void)console_protocol_write(text);
  }
}

void console_service_report_firmware_profile(
    const board_description_t *board,
    const char *role_name,
    const char *build_type)
{
  uint8_t storage[224];
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
  (void)console_protocol_append_bool(&message, board->fpga_fitted);
  (void)console_protocol_append_text(&message, ", CLOCK_DEVICE=");
  if (board->clock_device == BOARD_CLOCK_DEVICE_SIT5156) {
    (void)console_protocol_append_text(&message, "SiT5156");
  } else if (board->clock_device == BOARD_CLOCK_DEVICE_SIT3907) {
    (void)console_protocol_append_text(&message, "SiT3907");
  } else {
    (void)console_protocol_append_text(&message, "NONE");
  }
  (void)console_protocol_append_text(&message, ", EXT_TIMER=");
  (void)console_protocol_append_bool(
      &message,
      board->external_clock_counter_connected);
  (void)console_protocol_append_text(&message, ", RF_PATHS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)board->available_rf_paths,
      1U);
  (void)console_service_send(&message);
}

void console_service_report_clock(const clock_service_state_t *state)
{
  uint8_t storage[256];
  console_protocol_message_t message;

  if (state == NULL) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(
      &message,
      (state->status == CLOCK_SERVICE_STATUS_OK)
          ? "CLOCK INIT OK"
          : "CLOCK INIT ERROR");
  (void)console_protocol_append_text(&message, ", STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->status,
      2U);
  (void)console_protocol_append_text(&message, ", XO=");
  (void)console_protocol_append_text(
      &message,
      (state->part_name != NULL) ? state->part_name : "NONE");
  (void)console_protocol_append_text(&message, ", OE=");
  (void)console_protocol_append_bool(&message, state->output_enabled);
  (void)console_protocol_append_text(&message, ", READBACK=");
  (void)console_protocol_append_bool(
      &message,
      state->register_readback_verified);
  if (state->clock_device == BOARD_CLOCK_DEVICE_SIT5156) {
    (void)console_protocol_append_text(&message, ", ADDR=0x");
    (void)console_protocol_append_hex(
        &message,
        state->i2c_address_7bit,
        2U);
    (void)console_protocol_append_text(&message, ", REG0=0x");
    (void)console_protocol_append_hex(
        &message,
        state->frequency_lsw,
        4U);
    (void)console_protocol_append_text(&message, ", REG1=0x");
    (void)console_protocol_append_hex(
        &message,
        state->frequency_msw_oe,
        4U);
    (void)console_protocol_append_text(&message, ", REG2=0x");
    (void)console_protocol_append_hex(
        &message,
        state->pull_range_register,
        4U);
  }
  (void)console_protocol_append_text(&message, ", TIMER_CHECK=");
  (void)console_protocol_append_bool(
      &message,
      state->external_counter_checked);
  (void)console_protocol_append_text(&message, ", TIMER_DELTA=0x");
  (void)console_protocol_append_hex(
      &message,
      state->external_counter_delta,
      8U);
  (void)console_protocol_append_text(&message, ", CLOCK_DETECTED=");
  (void)console_protocol_append_bool(
      &message,
      state->output_clock_detected);
  (void)console_service_send(&message);
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
      (uint8_t)state->image_status,
      2U);
  (void)console_protocol_append_text(&message, ", IMG_LEN=0x");
  (void)console_protocol_append_hex(
      &message,
      state->image.image.length,
      8U);
  (void)console_protocol_append_text(&message, ", SYNC=0x");
  (void)console_protocol_append_hex(&message, state->image.sync_offset, 8U);
  (void)console_service_send(&message);
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
      (state->configure_status == ICE40UP5K_STATUS_OK)
          ? "FPGA CONFIG OK"
          : "FPGA CONFIG ERROR");
  (void)console_protocol_append_text(&message, ", STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->configure_status,
      2U);
  (void)console_protocol_append_text(&message, ", ATTEMPTS=");
  (void)console_protocol_append_hex(&message, state->attempt_count, 1U);
  (void)console_protocol_append_text(&message, ", CDONE_AT_RST=");
  (void)console_protocol_append_bool(
      &message,
      state->result.cdone_at_reset);
  (void)console_protocol_append_text(&message, ", CDONE_CLK=0x");
  (void)console_protocol_append_hex(
      &message,
      state->result.cdone_clocks,
      8U);
  (void)console_protocol_append_text(&message, ", CDONE=");
  (void)console_protocol_append_bool(&message, state->cdone_pin);
  (void)console_service_send(&message);
}

void console_service_report_uwb(const uwb_service_state_t *state)
{
  uint8_t storage[128];
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
  (void)console_service_send(&message);
}

void console_service_report_uwb_config(const uwb_service_state_t *state)
{
  uint8_t storage[320];
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
  (void)console_protocol_append_text(&message, ", STS=");
  if (state->radio_config.sts_mode == DW3000_STS_OFF) {
    (void)console_protocol_append_text(&message, "OFF");
  } else {
    (void)console_protocol_append_text(
        &message,
        (state->radio_config.sts_mode == DW3000_STS_MODE_1)
            ? "MODE1"
            : "MODE2");
    if (state->radio_config.sts_sdc) {
      (void)console_protocol_append_text(&message, "-SDC");
    }
    (void)console_protocol_append_text(&message, ", STS_LEN=0x");
    (void)console_protocol_append_hex(
        &message,
        state->radio_config.sts_length,
        4U);
  }
  if (state->mode == UWB_SERVICE_MODE_PERIODIC_TX) {
    (void)console_protocol_append_text(&message, ", MODE=TX, PERIOD_US=0x");
    (void)console_protocol_append_hex(
        &message,
        state->transmit_interval_us,
        8U);
  } else if (state->mode == UWB_SERVICE_MODE_CONTINUOUS_RX) {
    if (state->cir_capture_enabled) {
      (void)console_protocol_append_text(
          &message,
          ", MODE=RX_CIR, CIR=0x");
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
      (void)console_protocol_append_text(
          &message,
          ", MODE=RX_TIMING");
    }
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
  (void)console_protocol_append_text(&message, ", RF_MODE=");
  (void)console_protocol_append_text(
      &message,
      uwb_rf_mode_name(state->radio_config.rf_mode));
  (void)console_protocol_append_text(&message, ", PDOA_MODE=");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)state->radio_config.pdoa_mode,
      1U);
  (void)console_service_send(&message);

  if (state->config_status == DW3000_STATUS_CONFIGURATION_ERROR) {
    console_protocol_message_init(&message, storage, sizeof(storage));
    (void)console_protocol_append_text(&message, "UWB CFG DIAG, SDK_STATUS=");
    (void)console_protocol_append_i32(
        &message,
        state->device.config_sdk_status);
    (void)console_protocol_append_text(&message, ", SPI_STATUS=");
    (void)console_protocol_append_i32(
        &message,
        state->device.config_transport_status);
    (void)console_protocol_append_text(&message, ", PLL_STATUS=0x");
    (void)console_protocol_append_hex(
        &message,
        state->device.config_pll_status,
        8U);
    (void)console_protocol_append_text(&message, ", XTAL=");
    (void)console_protocol_append_bool(
        &message,
        state->device.config_xtal_settled);
    (void)console_protocol_append_text(&message, ", PLL_LOCK=");
    (void)console_protocol_append_bool(
        &message,
        state->device.config_pll_locked);
    (void)console_protocol_append_text(&message, ", PLL_CAL=");
    (void)console_protocol_append_bool(
        &message,
        state->device.config_pll_calibration_done);
    (void)console_protocol_append_text(&message, ", SYS_LO=0x");
    (void)console_protocol_append_hex(
        &message,
        state->device.config_system_status_low,
        8U);
    (void)console_protocol_append_text(&message, ", SYS_HI=0x");
    (void)console_protocol_append_hex(
        &message,
        state->device.config_system_status_high,
        8U);
    (void)console_protocol_append_text(&message, ", IDLE_RC=");
    (void)console_protocol_append_bool(
        &message,
        state->device.config_idle_rc);
    (void)console_service_send(&message);
  }
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
  (void)console_service_send(&message);
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
  (void)console_service_send(&message);
}

void console_service_report_do_clock_tracking_config(
    const do_clock_tracking_state_t *state,
    const uwb_timestamp_estimator_state_t *timestamp_state,
    serial_capture_format_t capture_format)
{
  uint8_t storage[320];
  console_protocol_message_t message;

  if ((state == NULL) || (timestamp_state == NULL)) {
    return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(&message, "DO TRACK CFG, ENABLE=");
  (void)console_protocol_append_bool(&message, state->enabled);
  (void)console_protocol_append_text(&message, ", READY=");
  (void)console_protocol_append_bool(&message, state->initialized);
  (void)console_protocol_append_text(&message, ", LOOP=");
  (void)console_protocol_append_text(
      &message,
      do_loop_strategy_name(state->strategy));
  (void)console_protocol_append_text(&message, ", TIMESTAMP=");
  (void)console_protocol_append_text(&message, timestamp_state->name);
  (void)console_protocol_append_text(&message, ", WINDOW=");
  (void)console_protocol_append_i32(
      &message,
      (int32_t)state->window_intervals);
  (void)console_protocol_append_text(&message, ", MAX_ERR_PPB=");
  (void)console_protocol_append_i32(
      &message,
      (int32_t)DO_CLOCK_TRACKING_MAX_ERROR_PPB);
  (void)console_protocol_append_text(&message, ", CMD_LIMIT_PPB=");
  (void)console_protocol_append_i32(
      &message,
      state->command_limit_ppb);
  (void)console_protocol_append_text(&message, ", CAPTURE_OUTPUT=");
  (void)console_protocol_append_text(
      &message,
      capture_output_format_name(capture_format));
  (void)console_protocol_append_text(&message, ", CIR_CAPTURE=");
  (void)console_protocol_append_bool(
      &message,
      timestamp_state->requires_cir ||
          capture_output_requires_cir(capture_format));
  (void)console_service_send(&message);
}

void console_service_report_do_clock_tracking(
    const do_clock_tracking_event_t *event,
    const do_clock_tracking_state_t *state)
{
  uint8_t storage[320];
  console_protocol_message_t message;
  const char *result;

  if ((event == NULL) || (state == NULL) ||
      (event->type == DO_CLOCK_TRACKING_EVENT_NONE)) {
    return;
  }

  switch (event->type) {
    case DO_CLOCK_TRACKING_EVENT_UPDATE:
      result = "UPDATE";
      break;
    case DO_CLOCK_TRACKING_EVENT_OUTLIER:
      result = "OUTLIER";
      break;
    case DO_CLOCK_TRACKING_EVENT_CLOCK_ERROR:
      result = "CLOCK_ERROR";
      break;
    default:
      return;
  }

  console_protocol_message_init(&message, storage, sizeof(storage));
  (void)console_protocol_append_text(&message, "DO TRACK, RESULT=");
  (void)console_protocol_append_text(&message, result);
  (void)console_protocol_append_text(&message, ", SEQ0=0x");
  (void)console_protocol_append_hex(
      &message,
      event->reference_sequence,
      8U);
  (void)console_protocol_append_text(&message, ", SEQ1=0x");
  (void)console_protocol_append_hex(&message, event->sequence, 8U);
  (void)console_protocol_append_text(&message, ", N=");
  (void)console_protocol_append_i32(
      &message,
      (int32_t)event->interval_count);
  (void)console_protocol_append_text(&message, ", TX_DT=0x");
  (void)console_protocol_append_hex64(
      &message,
      event->leader_delta_dtu,
      10U);
  (void)console_protocol_append_text(&message, ", RX_DT=0x");
  (void)console_protocol_append_hex64(
      &message,
      event->follower_delta_dtu,
      10U);
  (void)console_protocol_append_text(&message, ", ERR_PPB=");
  if (event->type == DO_CLOCK_TRACKING_EVENT_OUTLIER) {
    (void)console_protocol_append_text(&message, "OUT_OF_RANGE");
  } else {
    (void)console_protocol_append_i32(
        &message,
        event->measured_error_ppb);
  }
  (void)console_protocol_append_text(&message, ", CMD_PPB=");
  (void)console_protocol_append_i32(&message, event->command_ppb);
  (void)console_protocol_append_text(&message, ", CLK_STATUS=0x");
  (void)console_protocol_append_hex(
      &message,
      (uint8_t)event->clock_status,
      2U);
  (void)console_protocol_append_text(&message, ", OBS=");
  (void)console_protocol_append_i32(
      &message,
      (int32_t)state->last_strategy_observation_count);
  (void)console_protocol_append_text(&message, ", UPDATES=0x");
  (void)console_protocol_append_hex(&message, state->update_count, 8U);
  (void)console_protocol_append_text(&message, ", REJECT=0x");
  (void)console_protocol_append_hex(
      &message,
      state->rejected_window_count,
      8U);
  (void)console_service_send(&message);
}

static const char *uwb_rf_mode_name(dw3000_rf_mode_t mode)
{
  switch (mode) {
    case DW3000_RF_MODE_MANUAL_1:
      return "MANUAL_1";
    case DW3000_RF_MODE_MANUAL_2:
      return "MANUAL_2";
    case DW3000_RF_MODE_AUTO_1_2:
      return "AUTO_1_2";
    case DW3000_RF_MODE_AUTO_2_1:
      return "AUTO_2_1";
    default:
      return "INVALID";
  }
}

static board_status_t console_service_send(
    console_protocol_message_t *message)
{
  return s_boot_mode
      ? console_protocol_send_with_crc_blocking(message)
      : console_protocol_send_with_crc(message);
}
