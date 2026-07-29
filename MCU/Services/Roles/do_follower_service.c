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

static bool s_cir_export_active;

static void start_fpga(void);
static void start_uwb(void);

const char *role_service_name(void)
{
  return "DO-Follower";
}

void role_service_init(void)
{
  s_cir_export_active = false;
  start_fpga();
  start_uwb();
}

void role_service_process(void)
{
  const uwb_service_cir_capture_t *capture;

  if (s_cir_export_active &&
      !console_service_uwb_cir_export_busy()) {
    (void)uwb_service_release_cir_capture();
    s_cir_export_active = false;
  }

  uwb_service_process();
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

static void start_uwb(void)
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
    return;
  }

  (void)uwb_service_start_cir_receive(&g_uwb_default_profile);
  console_service_report_uwb_config(uwb_service_get_state());
}
