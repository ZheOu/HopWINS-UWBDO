/**
  ******************************************************************************
  * @file           : uwb_sts_diagnostic_service.c
  * @brief          : STS transmit and dual-antenna receive diagnostic workflow
  ******************************************************************************
  */

#include "uwb_sts_diagnostic_service.h"

#include "board.h"
#include "capture_output.h"
#include "clock_service.h"
#include "console_service.h"
#include "uwb_service.h"

#define STS_DIAGNOSTIC_RETRY_INTERVAL_MS UINT32_C(1000)
#define STS_DIAGNOSTIC_RX_HEALTH_INTERVAL_MS UINT32_C(5000)

static app_config_t s_config;
static uwb_profile_t s_profile;
static uwb_sts_diagnostic_role_t s_role;
static bool s_clock_running;
static bool s_uwb_running;
static bool s_capture_output_active;
static uint32_t s_next_clock_retry_ms;
static uint32_t s_next_uwb_retry_ms;
static uint32_t s_next_rx_health_ms;
static const uwb_service_time_source_t s_time_source = {
  .get_monotonic_time_ms = board_get_time_ms,
  .get_reference_time_ms = board_get_reference_time_ms,
};

static bool start_clock(void);
static bool start_uwb(void);
static void process_tx(void);
static void process_rx(void);

void uwb_sts_diagnostic_service_init(
    uwb_sts_diagnostic_role_t role,
    const app_config_t *config,
    const uwb_profile_t *profile)
{
  if ((config == NULL) || (profile == NULL)) {
    return;
  }

  s_config = *config;
  s_profile = *profile;
  s_role = role;
  s_clock_running = false;
  s_uwb_running = false;
  s_capture_output_active = false;
  s_next_clock_retry_ms = 0U;
  s_next_uwb_retry_ms = 0U;
  s_next_rx_health_ms = 0U;

  s_clock_running = start_clock();
  if (s_clock_running) {
    s_uwb_running = start_uwb();
  }
  if (!s_clock_running) {
    s_next_clock_retry_ms =
        board_get_time_ms() + STS_DIAGNOSTIC_RETRY_INTERVAL_MS;
  } else if (!s_uwb_running) {
    s_next_uwb_retry_ms =
        board_get_time_ms() + STS_DIAGNOSTIC_RETRY_INTERVAL_MS;
  }
}

void uwb_sts_diagnostic_service_process(void)
{
  uint32_t current_time_ms = board_get_time_ms();

  if (!s_clock_running) {
    if ((int32_t)(current_time_ms - s_next_clock_retry_ms) >= 0) {
      console_service_write("STS DIAG: retrying clock initialization\r\n");
      s_clock_running = start_clock();
      s_next_clock_retry_ms =
          current_time_ms + STS_DIAGNOSTIC_RETRY_INTERVAL_MS;
      if (s_clock_running) {
        s_uwb_running = start_uwb();
        if (!s_uwb_running) {
          s_next_uwb_retry_ms =
              board_get_time_ms() + STS_DIAGNOSTIC_RETRY_INTERVAL_MS;
        }
      }
    }
    return;
  }

  if (!s_uwb_running) {
    if ((int32_t)(current_time_ms - s_next_uwb_retry_ms) >= 0) {
      console_service_write("STS DIAG: retrying UWB initialization\r\n");
      s_uwb_running = start_uwb();
      s_next_uwb_retry_ms =
          current_time_ms + STS_DIAGNOSTIC_RETRY_INTERVAL_MS;
    }
    return;
  }

  if (s_role == UWB_STS_DIAGNOSTIC_TX) {
    process_tx();
  } else {
    process_rx();
  }
}

static bool start_clock(void)
{
  const board_description_t *board = board_get_description();
  clock_service_status_t status;

  if (board == NULL) {
    return false;
  }
  if (board->clock.device == BOARD_CLOCK_DEVICE_NONE) {
    console_service_write("STS DIAG: using fixed board UWB clock\r\n");
    return true;
  }

  console_service_write("STS DIAG: initializing selected UWB clock\r\n");
  status = clock_service_init();
  console_service_report_clock(clock_service_get_state());
  return status == CLOCK_SERVICE_STATUS_OK;
}

static bool start_uwb(void)
{
  const uwb_service_cir_config_t cir_config = {
    .sample_count = s_config.cir_sample_count,
    .pre_first_path_samples = s_config.cir_pre_first_path_samples,
    .mode = UWB_SERVICE_CIR_CAPTURE_STS_DUAL,
    .capture_cir = true,
  };
  dw3000_status_t status;

  console_service_write("STS DIAG: initializing DW3220 over SPI1\r\n");
  status = uwb_service_init(board_uwb_get_platform(), &s_time_source);
  console_service_report_uwb(uwb_service_get_state());
  if (status != DW3000_STATUS_OK) {
    return false;
  }

  if (s_role == UWB_STS_DIAGNOSTIC_TX) {
    console_service_write(
        "STS DIAG TX: starting 100 ms STS-SDC transmission\r\n");
    status = uwb_service_start_periodic_transmit(&s_profile);
  } else {
    console_service_write(
        "STS DIAG RX: Mode 3, exporting dual-path STS0+STS1 as HCIR v3\r\n");
    status = uwb_service_start_cir_receive(&s_profile, &cir_config);
    s_next_rx_health_ms =
        board_get_time_ms() + STS_DIAGNOSTIC_RX_HEALTH_INTERVAL_MS;
  }
  console_service_report_uwb_config(uwb_service_get_state());
  return status == DW3000_STATUS_OK;
}

static void process_tx(void)
{
  uwb_service_tx_event_t event;

  uwb_service_process();
  if (uwb_service_take_tx_event(&event)) {
    console_service_report_uwb_tx(&event);
  }
}

static void process_rx(void)
{
  const uwb_service_cir_capture_t *capture;
  const uwb_service_state_t *state;
  uwb_timestamp_result_t timestamp;
  board_status_t output_status;
  uint32_t current_time_ms = board_get_time_ms();

  if (s_capture_output_active && !capture_output_busy()) {
    (void)uwb_service_release_cir_capture();
    s_capture_output_active = false;
  }

  uwb_service_process();
  state = uwb_service_get_state();
  if (!state->cir_receive_enabled) {
    if (!s_capture_output_active) {
      s_uwb_running = false;
      s_next_uwb_retry_ms =
          current_time_ms + STS_DIAGNOSTIC_RETRY_INTERVAL_MS;
      console_service_write("STS DIAG RX: receiver stopped; scheduling reinit\r\n");
    }
    return;
  }

  if ((int32_t)(current_time_ms - s_next_rx_health_ms) >= 0) {
    console_service_report_uwb_rx_health(state);
    s_next_rx_health_ms =
        current_time_ms + STS_DIAGNOSTIC_RX_HEALTH_INTERVAL_MS;
  }
  if (s_capture_output_active) {
    return;
  }

  capture = uwb_service_get_cir_capture();
  if (capture == NULL) {
    return;
  }

  timestamp = (uwb_timestamp_result_t){
    .status = UWB_TIMESTAMP_RESULT_OK,
    .estimator = UWB_TIMESTAMP_ESTIMATOR_DW_ADJUSTED,
    .timestamp_dtu = capture->receive_timestamp,
    .capture_id = capture->capture_id,
    .quality_q15 = UINT16_MAX,
  };
  output_status = capture_output_start(
      capture,
      &timestamp,
      SERIAL_CAPTURE_FORMAT_HCIR_V3);
  if (output_status == BOARD_OK) {
    s_capture_output_active = true;
  } else if (output_status != BOARD_BUSY) {
    console_service_write("STS DIAG RX: HCIR v3 encoder rejected capture\r\n");
    (void)uwb_service_release_cir_capture();
  }
}
