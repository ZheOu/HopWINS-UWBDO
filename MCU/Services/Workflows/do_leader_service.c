/**
  ******************************************************************************
  * @file           : do_leader_service.c
  * @brief          : DO leader application workflow
  ******************************************************************************
  */

#include "do_leader_service.h"

#include "board.h"
#include "clock_service.h"
#include "console_service.h"
#include "uwb_service.h"

#define LEADER_CLOCK_RETRY_INTERVAL_MS UINT32_C(1000)
#define LEADER_UWB_RETRY_INTERVAL_MS UINT32_C(1000)

static bool s_clock_running;
static bool s_uwb_running;
static uint32_t s_next_clock_retry_ms;
static uint32_t s_next_uwb_retry_ms;
static uwb_profile_t s_uwb_profile;
static const uwb_service_time_source_t s_uwb_time_source = {
  .get_monotonic_time_ms = board_get_time_ms,
  .get_reference_time_ms = board_get_reference_time_ms,
};

static bool start_clock(void);
static bool start_uwb(void);

void do_leader_service_init(
    const app_config_t *config,
    const uwb_profile_t *uwb_profile)
{
  const board_description_t *board = board_get_description();

  if ((config == NULL) || (uwb_profile == NULL) || (board == NULL)) {
    return;
  }

  s_uwb_profile = *uwb_profile;
  s_clock_running =
      board->clock.device == BOARD_CLOCK_DEVICE_NONE;
  s_uwb_running = false;
  s_next_clock_retry_ms = 0U;
  s_next_uwb_retry_ms = 0U;

  if (!s_clock_running) {
    s_clock_running = start_clock();
  }
  if (s_clock_running) {
    s_uwb_running = start_uwb();
  }
  if (!s_clock_running) {
    s_next_clock_retry_ms =
        board_get_time_ms() + LEADER_CLOCK_RETRY_INTERVAL_MS;
  } else if (!s_uwb_running) {
    s_next_uwb_retry_ms =
        board_get_time_ms() + LEADER_UWB_RETRY_INTERVAL_MS;
  }
}

void do_leader_service_process(void)
{
  uwb_service_tx_event_t tx_event;
  uint32_t current_time_ms = board_get_time_ms();

  if (!s_clock_running) {
    if ((int32_t)(current_time_ms - s_next_clock_retry_ms) >= 0) {
      console_service_write("CLOCK: retrying initialization\r\n");
      s_clock_running = start_clock();
      s_next_clock_retry_ms =
          current_time_ms + LEADER_CLOCK_RETRY_INTERVAL_MS;
      if (s_clock_running) {
        s_uwb_running = start_uwb();
        s_next_uwb_retry_ms =
            board_get_time_ms() + LEADER_UWB_RETRY_INTERVAL_MS;
      }
    }
    return;
  }

  if (!s_uwb_running) {
    if ((int32_t)(current_time_ms - s_next_uwb_retry_ms) >= 0) {
      console_service_write("UWB TX: retrying initialization\r\n");
      s_uwb_running = start_uwb();
      s_next_uwb_retry_ms =
          current_time_ms + LEADER_UWB_RETRY_INTERVAL_MS;
    }
    return;
  }

  uwb_service_process();
  if (uwb_service_take_tx_event(&tx_event)) {
    console_service_report_uwb_tx(&tx_event);
  }
}

static bool start_clock(void)
{
  clock_service_status_t status;

  console_service_write("CLOCK: initializing Leader local oscillator\r\n");
  status = clock_service_init();
  console_service_report_clock(clock_service_get_state());
  if (status == CLOCK_SERVICE_STATUS_OK) {
    console_service_write(
        "CLOCK: Leader reference output verified\r\n");
    return true;
  }

  console_service_write(
      "CLOCK: Leader reference unavailable; UWB TX held off\r\n");
  return false;
}

static bool start_uwb(void)
{
  dw3000_status_t status;

  console_service_write("UWB: initializing DW3220 over SPI1\r\n");
  status = uwb_service_init(
      board_uwb_get_platform(),
      &s_uwb_time_source);
  console_service_report_uwb(uwb_service_get_state());
  if (status != DW3000_STATUS_OK) {
    return false;
  }

  console_service_write("UWB: configuring 100 ms periodic transmit\r\n");
  status = uwb_service_start_periodic_transmit(&s_uwb_profile);
  console_service_report_uwb_config(uwb_service_get_state());
  return status == DW3000_STATUS_OK;
}
