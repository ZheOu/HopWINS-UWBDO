/**
  ******************************************************************************
  * @file           : do_follower_service.c
  * @brief          : DO follower application workflow
  ******************************************************************************
  */

#include "do_follower_service.h"

#include "board.h"
#include "capture_output.h"
#include "clock_service.h"
#include "console_service.h"
#include "do_clock_tracking_service.h"
#include "fpga_service.h"
#include "uwb_service.h"
#include "uwb_timestamp_estimator.h"

#define FOLLOWER_CLOCK_RETRY_INTERVAL_MS UINT32_C(1000)
#define FOLLOWER_UWB_RETRY_INTERVAL_MS UINT32_C(1000)
#define FOLLOWER_RX_HEALTH_INTERVAL_MS UINT32_C(5000)

static bool s_capture_output_active;
static bool s_clock_running;
static bool s_uwb_running;
static uint32_t s_next_clock_retry_ms;
static uint32_t s_next_uwb_retry_ms;
static uint32_t s_next_rx_health_ms;
static uint32_t s_next_rf_ab_switch_ms;
static app_config_t s_config;
static uwb_profile_t s_uwb_profile;
static const uwb_service_time_source_t s_uwb_time_source = {
  .get_monotonic_time_ms = board_get_time_ms,
  .get_reference_time_ms = board_get_reference_time_ms,
};

static bool start_clock(void);
static void start_fpga(void);
static bool start_uwb(void);
static bool switch_rf_ab_port(void);
static dw3000_status_t start_cir_receive(void);

void do_follower_service_init(
    const app_config_t *config,
    const uwb_profile_t *uwb_profile)
{
  if ((config == NULL) || (uwb_profile == NULL)) {
    return;
  }

  s_config = *config;
  s_uwb_profile = *uwb_profile;
  if (s_config.follower_rf_ab_test) {
    s_uwb_profile.radio.rf_mode = DW3000_RF_MODE_MANUAL_1;
  }
  s_capture_output_active = false;
  s_clock_running = false;
  s_uwb_running = false;
  s_next_clock_retry_ms = 0U;
  s_next_uwb_retry_ms = 0U;
  s_next_rx_health_ms = 0U;
  s_next_rf_ab_switch_ms = 0U;
  s_clock_running = start_clock();
  if (s_clock_running) {
    start_fpga();
    s_uwb_running = start_uwb();
  }
  if (s_clock_running && !s_uwb_running) {
    s_next_uwb_retry_ms =
        board_get_time_ms() + FOLLOWER_UWB_RETRY_INTERVAL_MS;
  } else if (!s_clock_running) {
    s_next_clock_retry_ms =
        board_get_time_ms() + FOLLOWER_CLOCK_RETRY_INTERVAL_MS;
  }
}

void do_follower_service_process(void)
{
  const uwb_service_state_t *state;
  const uwb_service_cir_capture_t *capture;
  board_status_t output_status;
  do_clock_tracking_event_t tracking_event;
  uwb_timestamp_result_t timestamp;
  uint32_t current_time_ms = board_get_time_ms();

  if (!s_clock_running) {
    if ((int32_t)(current_time_ms - s_next_clock_retry_ms) >= 0) {
      console_service_write("CLOCK: retrying initialization\r\n");
      s_clock_running = start_clock();
      s_next_clock_retry_ms =
          current_time_ms + FOLLOWER_CLOCK_RETRY_INTERVAL_MS;
      if (s_clock_running) {
        start_fpga();
        s_uwb_running = start_uwb();
        s_next_uwb_retry_ms =
            board_get_time_ms() + FOLLOWER_UWB_RETRY_INTERVAL_MS;
        s_next_rx_health_ms =
            board_get_time_ms() + FOLLOWER_RX_HEALTH_INTERVAL_MS;
      }
    }
    return;
  }

  if (!s_uwb_running) {
    if ((int32_t)(current_time_ms - s_next_uwb_retry_ms) >= 0) {
      console_service_write("UWB RX: retrying initialization\r\n");
      s_uwb_running = start_uwb();
      s_next_uwb_retry_ms =
          current_time_ms + FOLLOWER_UWB_RETRY_INTERVAL_MS;
      s_next_rx_health_ms =
          current_time_ms + FOLLOWER_RX_HEALTH_INTERVAL_MS;
    }
    return;
  }

  if (s_capture_output_active && !capture_output_busy()) {
    (void)uwb_service_release_cir_capture();
    s_capture_output_active = false;
  }

  state = uwb_service_get_state();
  if (s_config.follower_rf_ab_test &&
      (s_config.follower_rf_ab_interval_ms != 0U) &&
      !s_capture_output_active &&
      !capture_output_busy() &&
      (state->queued_capture_count == 0U) &&
      ((int32_t)(current_time_ms - s_next_rf_ab_switch_ms) >= 0)) {
    if (!switch_rf_ab_port()) {
      s_uwb_running = false;
      s_next_uwb_retry_ms =
          current_time_ms + FOLLOWER_UWB_RETRY_INTERVAL_MS;
    }
    return;
  }

  uwb_service_process();
  state = uwb_service_get_state();
  if (!state->cir_receive_enabled) {
    if (s_capture_output_active) {
      return;
    }
    s_uwb_running = false;
    s_next_uwb_retry_ms =
        current_time_ms + FOLLOWER_UWB_RETRY_INTERVAL_MS;
    console_service_write("UWB RX: state lost, scheduling reinit\r\n");
    return;
  }

  if ((int32_t)(current_time_ms - s_next_rx_health_ms) >= 0) {
    console_service_report_uwb_rx_health(state);
    s_next_rx_health_ms =
        current_time_ms + FOLLOWER_RX_HEALTH_INTERVAL_MS;
  }
  if (s_capture_output_active) {
    return;
  }

  capture = uwb_service_get_cir_capture();
  if (capture != NULL) {
    if ((uwb_timestamp_estimator_process(capture, &timestamp) ==
         UWB_TIMESTAMP_RESULT_OK) &&
        do_clock_tracking_service_process_capture(
            capture,
            timestamp.timestamp_dtu,
            &tracking_event)) {
      console_service_report_do_clock_tracking(
          &tracking_event,
          do_clock_tracking_service_get_state());
    }

    if (s_config.follower_capture_format == SERIAL_CAPTURE_FORMAT_OFF) {
      (void)uwb_service_release_cir_capture();
    } else {
      output_status = capture_output_start(
          capture,
          &timestamp,
          s_config.follower_capture_format);
      if (output_status == BOARD_OK) {
        s_capture_output_active = true;
      } else if (output_status != BOARD_BUSY) {
        console_service_write(
            "UWB RX: capture encoder rejected a frame\r\n");
        (void)uwb_service_release_cir_capture();
      }
    }
  }
}

static bool start_clock(void)
{
  const do_clock_tracking_config_t tracking_config = {
    .enabled = s_config.do_clock_tracking,
    .strategy = s_config.do_loop_strategy,
    .window_intervals = s_config.do_loop_window_intervals,
  };
  clock_service_status_t status;

  console_service_write("CLOCK: initializing selected local oscillator\r\n");
  status = clock_service_init();
  console_service_report_clock(clock_service_get_state());
  if (status == CLOCK_SERVICE_STATUS_OK) {
    if (!uwb_timestamp_estimator_init(s_config.timestamp_estimator)) {
      console_service_write(
          "DO TRACK: selected timestamp estimator is invalid\r\n");
      return false;
    }
    if (!do_clock_tracking_service_init(&tracking_config)) {
      console_service_write(
          "DO TRACK: selected clock control is not ready\r\n");
      return false;
    }
    console_service_report_do_clock_tracking_config(
        do_clock_tracking_service_get_state(),
        uwb_timestamp_estimator_get_state(),
        s_config.follower_capture_format);
    if (clock_service_get_state()->clock_device ==
        BOARD_CLOCK_DEVICE_SIT5156) {
      console_service_write(
          "CLOCK: register readback and physical output verified\r\n");
    } else {
      console_service_write("CLOCK: physical output verified\r\n");
    }
    return true;
  }

  switch (status) {
    case CLOCK_SERVICE_STATUS_IO_ERROR:
      console_service_write(
          "CLOCK: I2C access failed; check 2.5 V, SCL/SDA pull-ups and address\r\n");
      break;
    case CLOCK_SERVICE_STATUS_UNEXPECTED_CONFIG:
      console_service_write(
          "CLOCK: register signature does not match the fitted SiT5156 option\r\n");
      break;
    case CLOCK_SERVICE_STATUS_VERIFY_FAILED:
      console_service_write(
          "CLOCK: control readback or TIM2 ETR clock detection failed\r\n");
      break;
    default:
      console_service_write("CLOCK: initialization failed\r\n");
      break;
  }
  return false;
}

static void start_fpga(void)
{
  const board_description_t *board = board_get_description();
  ice40up5k_status_t status;

  if ((board == NULL) || !board->fpga_fitted) {
    console_service_write(
        "FPGA: not fitted, skipping configuration\r\n");
    return;
  }

  (void)board_fpga_set_user_enabled(false);
  console_service_write("FPGA: validating embedded configuration image\r\n");
  status = fpga_service_init(board_fpga_get_platform());
  console_service_report_fpga_image(fpga_service_get_state());

  if (status != ICE40UP5K_STATUS_OK) {
    console_service_write("FPGA: aborted, image rejected, nothing sent\r\n");
    return;
  }

  console_service_write("FPGA: configuring iCE40UP5K over SPI\r\n");
  status = fpga_service_configure();
  console_service_report_fpga_result(fpga_service_get_state());

  switch (status) {
    case ICE40UP5K_STATUS_OK:
      if (board_fpga_set_user_enabled(true) == BOARD_OK) {
        console_service_write(
            "FPGA: CDONE high, user logic enabled\r\n");
      } else {
        console_service_write(
            "FPGA: configured, but user logic enable failed\r\n");
      }
      break;
    case ICE40UP5K_STATUS_CDONE_STUCK_HIGH:
      console_service_write(
          "FPGA: absent or CDONE/CRESET_B wiring fault\r\n");
      break;
    case ICE40UP5K_STATUS_CDONE_TIMEOUT:
      console_service_write(
          "FPGA: reset seen but CDONE never rose, image not accepted\r\n");
      break;
    case ICE40UP5K_STATUS_CDONE_DROPPED:
      console_service_write(
          "FPGA: CDONE rose then fell, internal CRC check failed\r\n");
      break;
    default:
      console_service_write("FPGA: configuration failed\r\n");
      break;
  }
}

static bool start_uwb(void)
{
  bool capture_cir =
      capture_output_requires_cir(s_config.follower_capture_format) ||
      uwb_timestamp_estimator_requires_cir(
          s_config.timestamp_estimator);
  dw3000_status_t status;

  console_service_write("UWB: initializing DW3220 over SPI1\r\n");
  status = uwb_service_init(
      board_uwb_get_platform(),
      &s_uwb_time_source);
  console_service_report_uwb(uwb_service_get_state());
  if (status != DW3000_STATUS_OK) {
    return false;
  }

  console_service_write(
      capture_cir
          ? "UWB: configuring continuous RX and CIR capture\r\n"
          : "UWB: configuring continuous RX timing capture\r\n");
  status = start_cir_receive();
  console_service_report_uwb_config(uwb_service_get_state());
  if ((status == DW3000_STATUS_OK) &&
      s_config.follower_rf_ab_test) {
    console_service_write(
        (s_uwb_profile.radio.rf_mode == DW3000_RF_MODE_MANUAL_1)
            ? "UWB RF A/B: RF1 window started\r\n"
            : "UWB RF A/B: RF2 window started\r\n");
  }
  return status == DW3000_STATUS_OK;
}

static bool switch_rf_ab_port(void)
{
  dw3000_status_t status;

  uwb_service_stop_cir_receive();
  s_uwb_profile.radio.rf_mode =
      (s_uwb_profile.radio.rf_mode == DW3000_RF_MODE_MANUAL_1)
          ? DW3000_RF_MODE_MANUAL_2
          : DW3000_RF_MODE_MANUAL_1;

  console_service_write(
      (s_uwb_profile.radio.rf_mode == DW3000_RF_MODE_MANUAL_1)
          ? "UWB RF A/B: switching to RF1\r\n"
          : "UWB RF A/B: switching to RF2\r\n");
  status = start_cir_receive();
  console_service_report_uwb_config(uwb_service_get_state());
  return status == DW3000_STATUS_OK;
}

static dw3000_status_t start_cir_receive(void)
{
  const uwb_service_cir_config_t cir_config = {
    .sample_count = s_config.cir_sample_count,
    .pre_first_path_samples = s_config.cir_pre_first_path_samples,
    .capture_cir =
        capture_output_requires_cir(s_config.follower_capture_format) ||
        uwb_timestamp_estimator_requires_cir(
            s_config.timestamp_estimator),
  };
  dw3000_status_t status = uwb_service_start_cir_receive(
      &s_uwb_profile,
      &cir_config);

  if ((status == DW3000_STATUS_OK) &&
      s_config.follower_rf_ab_test) {
    s_next_rf_ab_switch_ms =
        board_get_time_ms() + s_config.follower_rf_ab_interval_ms;
  }
  return status;
}
