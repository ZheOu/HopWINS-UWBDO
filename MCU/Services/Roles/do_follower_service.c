/**
  ******************************************************************************
  * @file           : do_follower_service.c
  * @brief          : DO follower application role
  ******************************************************************************
  */

#include "role_service.h"

#include "board.h"
#include "console_service.h"
#include "fpga_service.h"
#include "uwb_service.h"

#ifndef HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
#define HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC 0
#endif

#if HOPWINS_APP_ROLE != HOPWINS_APP_ROLE_DO_FOLLOWER
#error "do_follower_service.c compiled for the wrong application role"
#endif

#if !HOPWINS_BOARD_HAS_FPGA || !HOPWINS_BOARD_HAS_CLOCK_CONTROL
#error "The DO follower role requires FPGA and clock-control hardware"
#endif

#define FOLLOWER_UWB_RETRY_INTERVAL_MS UINT32_C(1000)
#define FOLLOWER_RX_HEALTH_INTERVAL_MS UINT32_C(5000)

static bool s_cir_export_active;
static bool s_uwb_running;
static uint32_t s_next_uwb_retry_ms;
static uint32_t s_next_rx_health_ms;

static void start_fpga(void);
static bool start_uwb(void);

const char *role_service_name(void)
{
  return "DO-Follower";
}

void role_service_init(void)
{
  s_cir_export_active = false;
  s_uwb_running = false;
  s_next_uwb_retry_ms = 0U;
  s_next_rx_health_ms = 0U;
  start_fpga();
  s_uwb_running = start_uwb();
  if (!s_uwb_running) {
    s_next_uwb_retry_ms =
        board_get_time_ms() + FOLLOWER_UWB_RETRY_INTERVAL_MS;
  }
}

void role_service_process(void)
{
  const uwb_service_state_t *state;
  const uwb_service_cir_capture_t *capture;
  uint32_t current_time_ms = board_get_time_ms();

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

  if (s_cir_export_active &&
      !console_service_uwb_cir_export_busy()) {
    (void)uwb_service_release_cir_capture();
    s_cir_export_active = false;
  }

  uwb_service_process();
  state = uwb_service_get_state();
  if (!state->cir_receive_enabled) {
    if (s_cir_export_active) {
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
  if (s_cir_export_active) {
    return;
  }

  capture = uwb_service_get_cir_capture();
  if ((capture != NULL) &&
      (console_service_start_uwb_cir_export(capture) == BOARD_OK)) {
    s_cir_export_active = true;
  }
}

static void start_fpga(void)
{
  ice40_status_t status;

  console_service_write("FPGA: checking embedded image at 0x081E0000\r\n");
  status = fpga_service_init(board_fpga_get_platform());
  console_service_report_fpga_image(fpga_service_get_state());

  if (status != ICE40_STATUS_OK) {
    console_service_write("FPGA: aborted, image rejected, nothing sent\r\n");
    return;
  }

  console_service_write("FPGA: sending bitstream over SPI1 at 12 MHz\r\n");
  status = fpga_service_configure();
  console_service_report_fpga_result(fpga_service_get_state());

  switch (status) {
    case ICE40_STATUS_OK:
      console_service_write("FPGA: CDONE high, device is in user mode\r\n");
      break;
    case ICE40_STATUS_CDONE_STUCK_HIGH:
      console_service_write(
          "FPGA: absent or CDONE/CRESET_B wiring fault\r\n");
      break;
    case ICE40_STATUS_CDONE_TIMEOUT:
      console_service_write(
          "FPGA: reset seen but CDONE never rose, image not accepted\r\n");
      break;
    case ICE40_STATUS_CDONE_DROPPED:
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
  dw3000_status_t status =
      uwb_service_init(board_uwb_get_platform());

#if HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
  if (status == DW3000_STATUS_OK) {
    (void)uwb_service_run_clock_diagnostic();
  }
#endif

  console_service_report_uwb(uwb_service_get_state());
  if (status != DW3000_STATUS_OK) {
    return false;
  }

  status = uwb_service_start_cir_receive(&g_uwb_default_profile);
  console_service_report_uwb_config(uwb_service_get_state());
  return status == DW3000_STATUS_OK;
}
