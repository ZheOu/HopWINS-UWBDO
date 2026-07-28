/**
  ******************************************************************************
  * @file           : console_service.c
  * @brief          : PC console reporting service
  ******************************************************************************
  */

#include "console_service.h"

#include "console_protocol.h"

void console_service_init(void)
{
  (void)console_protocol_write("\r\n=== HopWINS-UWBDO ===\r\n");
}

void console_service_process(void)
{
}

void console_service_write(const char *text)
{
  (void)console_protocol_write(text);
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
