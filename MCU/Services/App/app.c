/**
  ******************************************************************************
  * @file           : app.c
  * @brief          : Top-level application service
  ******************************************************************************
  */

#include "app.h"

#include "board.h"
#include "console_service.h"
#include "fpga_service.h"
#include "uwb_service.h"

#ifndef HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
#define HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC 1
#endif

#ifndef HOPWINS_ENABLE_PERIODIC_UWB_TX
#define HOPWINS_ENABLE_PERIODIC_UWB_TX 1
#endif

static void app_start_fpga(void);
static void app_start_uwb(void);

void app_init(void)
{
  console_service_init();
  app_start_fpga();
  app_start_uwb();
}

void app_process(void)
{
  uwb_service_tx_event_t tx_event;

  uwb_service_process();
  if (uwb_service_take_tx_event(&tx_event)) {
    console_service_report_uwb_tx(&tx_event);
  }
  console_service_process();
}

static void app_start_fpga(void)
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
          "FPGA: CDONE never went low in reset, CRESET_B/CDONE wiring\r\n");
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

static void app_start_uwb(void)
{
  (void)uwb_service_init(board_uwb_get_platform());

#if HOPWINS_ENABLE_BOOT_UWB_CLOCK_DIAGNOSTIC
  (void)uwb_service_run_clock_diagnostic();
#endif

  console_service_report_uwb(uwb_service_get_state());

#if HOPWINS_ENABLE_PERIODIC_UWB_TX
  (void)uwb_service_start_periodic_transmit(&g_uwb_default_profile);
  console_service_report_uwb_config(uwb_service_get_state());
#endif
}
