/**
  ******************************************************************************
  * @file           : clock_service.c
  * @brief          : Board-selected local clock control and verification
  ******************************************************************************
  */

#include "clock_service.h"

#include "sit3907.h"
#include "sit5156.h"

#include <string.h>

#define CLOCK_SERVICE_RATED_STABILITY_MS      UINT32_C(45)
#define CLOCK_SERVICE_COUNTER_SAMPLE_MS       UINT32_C(20)
#define CLOCK_SERVICE_SIT3907_PULL_LIMIT_PPB  INT32_C(1500000)

static sit3907_device_t s_sit3907;
static sit5156_device_t s_sit5156;
static clock_service_state_t s_state;

static clock_service_status_t init_sit3907(void);
static clock_service_status_t init_sit5156(void);
static clock_service_status_t verify_output_clock(void);
static clock_service_status_t map_sit3907_status(
    sit3907_status_t status);
static clock_service_status_t map_sit5156_status(
    sit5156_status_t status);
static void copy_sit5156_snapshot(void);
static void dp_drive_high(void);
static void dp_drive_low(void);
static void dp_release(void);
static bool i2c_write(
    uint8_t reg_address,
    const uint8_t *data,
    size_t len);
static bool i2c_read(
    uint8_t reg_address,
    uint8_t *data,
    size_t len);

static const sit3907_platform_t s_sit3907_platform = {
  .dp_drive_high = dp_drive_high,
  .dp_drive_low = dp_drive_low,
  .dp_release = dp_release,
  .delay_us = board_delay_us,
};

static const sit5156_platform_t s_sit5156_platform = {
  .write = i2c_write,
  .read = i2c_read,
  .delay_us = board_delay_us,
};

clock_service_status_t clock_service_init(void)
{
  const board_description_t *board = board_get_description();
  clock_service_status_t status;

  memset(&s_state, 0, sizeof(s_state));
  if (board == NULL) {
    s_state.status = CLOCK_SERVICE_STATUS_NO_DEVICE;
    return s_state.status;
  }
  s_state.clock_device = board->clock_device;

  switch (s_state.clock_device) {
    case BOARD_CLOCK_DEVICE_SIT5156:
      status = init_sit5156();
      break;
    case BOARD_CLOCK_DEVICE_SIT3907:
      status = init_sit3907();
      break;
    case BOARD_CLOCK_DEVICE_NONE:
    default:
      status = CLOCK_SERVICE_STATUS_NO_DEVICE;
      break;
  }

  if (status == CLOCK_SERVICE_STATUS_OK) {
    status = verify_output_clock();
  }
  s_state.status = status;
  s_state.initialized = status == CLOCK_SERVICE_STATUS_OK;
  return status;
}

clock_service_status_t clock_service_set_pull_ppb(int32_t ppb)
{
  clock_service_status_t status;

  if (!s_state.initialized) {
    return CLOCK_SERVICE_STATUS_NOT_INITIALIZED;
  }

  switch (s_state.clock_device) {
    case BOARD_CLOCK_DEVICE_SIT5156:
      status = map_sit5156_status(
          sit5156_set_pull_ppb(&s_sit5156, ppb));
      copy_sit5156_snapshot();
      break;
    case BOARD_CLOCK_DEVICE_SIT3907:
      status = map_sit3907_status(
          sit3907_set_pull_ppb(&s_sit3907, ppb));
      if (status == CLOCK_SERVICE_STATUS_OK) {
        s_state.pull_ppb = s_sit3907.pull_ppb;
        s_state.frequency_control_word =
            s_sit3907.frequency_control_word;
      }
      break;
    default:
      status = CLOCK_SERVICE_STATUS_NO_DEVICE;
      break;
  }

  s_state.status = status;
  return status;
}

const clock_service_state_t *clock_service_get_state(void)
{
  return &s_state;
}

static clock_service_status_t init_sit3907(void)
{
  sit3907_status_t driver_status;

  s_state.part_name = "SiT3907";
  s_state.output_enabled = true;
  s_state.pull_limit_ppb = CLOCK_SERVICE_SIT3907_PULL_LIMIT_PPB;
  driver_status = sit3907_init(&s_sit3907, &s_sit3907_platform);
  if (driver_status == SIT3907_STATUS_OK) {
    driver_status = sit3907_center(&s_sit3907);
  }
  if (driver_status == SIT3907_STATUS_OK) {
    s_state.frequency_control_word =
        s_sit3907.frequency_control_word;
    s_state.pull_ppb = s_sit3907.pull_ppb;
  }
  return map_sit3907_status(driver_status);
}

static clock_service_status_t init_sit5156(void)
{
  sit5156_status_t driver_status;

  s_state.part_name = "SiT5156";
  s_state.i2c_address_7bit = SIT5156_I2C_ADDRESS_7BIT;
  s_state.pull_limit_ppb = SIT5156_MAX_PULL_PPB;

  driver_status = sit5156_init(&s_sit5156, &s_sit5156_platform);
  copy_sit5156_snapshot();
  if (driver_status != SIT5156_STATUS_OK) {
    return map_sit5156_status(driver_status);
  }
  /* An MCU-only reset does not reset the continuously powered XO. Centering
     here makes every firmware start deterministic before enabling CLK. */
  driver_status = sit5156_center(&s_sit5156);
  if (driver_status == SIT5156_STATUS_OK) {
    driver_status = sit5156_set_output_enabled(&s_sit5156, true);
  }
  copy_sit5156_snapshot();
  if (driver_status != SIT5156_STATUS_OK) {
    return map_sit5156_status(driver_status);
  }
  if (!s_state.output_enabled ||
      (s_state.frequency_control_word != 0)) {
    return CLOCK_SERVICE_STATUS_VERIFY_FAILED;
  }
  s_state.register_readback_verified = true;

  /* The first pulse appears much sooner, but 45 ms is the datasheet maximum
     time to rated frequency stability after a cold power-up. */
  board_delay_ms(CLOCK_SERVICE_RATED_STABILITY_MS);
  return CLOCK_SERVICE_STATUS_OK;
}

static clock_service_status_t verify_output_clock(void)
{
  const board_description_t *board = board_get_description();
  uint32_t before;
  uint32_t after;

  if ((board == NULL) ||
      !board->external_clock_counter_connected) {
    return CLOCK_SERVICE_STATUS_OK;
  }

  s_state.external_counter_checked = true;
  before = board_external_clock_counter_get();
  board_delay_ms(CLOCK_SERVICE_COUNTER_SAMPLE_MS);
  after = board_external_clock_counter_get();
  s_state.external_counter_delta = after - before;
  s_state.output_clock_detected = s_state.external_counter_delta != 0U;

  return s_state.output_clock_detected
             ? CLOCK_SERVICE_STATUS_OK
             : CLOCK_SERVICE_STATUS_VERIFY_FAILED;
}

static clock_service_status_t map_sit3907_status(
    sit3907_status_t status)
{
  switch (status) {
    case SIT3907_STATUS_OK:
      return CLOCK_SERVICE_STATUS_OK;
    case SIT3907_STATUS_BAD_ARG:
      return CLOCK_SERVICE_STATUS_BAD_ARG;
    case SIT3907_STATUS_NOT_INITIALIZED:
      return CLOCK_SERVICE_STATUS_NOT_INITIALIZED;
    case SIT3907_STATUS_OUT_OF_RANGE:
      return CLOCK_SERVICE_STATUS_OUT_OF_RANGE;
    default:
      return CLOCK_SERVICE_STATUS_IO_ERROR;
  }
}

static clock_service_status_t map_sit5156_status(
    sit5156_status_t status)
{
  switch (status) {
    case SIT5156_STATUS_OK:
      return CLOCK_SERVICE_STATUS_OK;
    case SIT5156_STATUS_BAD_ARG:
      return CLOCK_SERVICE_STATUS_BAD_ARG;
    case SIT5156_STATUS_IO_ERROR:
      return CLOCK_SERVICE_STATUS_IO_ERROR;
    case SIT5156_STATUS_NOT_INITIALIZED:
      return CLOCK_SERVICE_STATUS_NOT_INITIALIZED;
    case SIT5156_STATUS_OUT_OF_RANGE:
      return CLOCK_SERVICE_STATUS_OUT_OF_RANGE;
    case SIT5156_STATUS_UNEXPECTED_CONFIG:
      return CLOCK_SERVICE_STATUS_UNEXPECTED_CONFIG;
    case SIT5156_STATUS_VERIFY_FAILED:
    default:
      return CLOCK_SERVICE_STATUS_VERIFY_FAILED;
  }
}

static void copy_sit5156_snapshot(void)
{
  s_state.frequency_lsw = s_sit5156.snapshot.frequency_lsw;
  s_state.frequency_msw_oe = s_sit5156.snapshot.frequency_msw_oe;
  s_state.pull_range_register =
      s_sit5156.snapshot.pull_range_register;
  s_state.frequency_control_word =
      s_sit5156.snapshot.frequency_control_word;
  s_state.pull_ppb = s_sit5156.snapshot.pull_ppb;
  s_state.output_enabled = s_sit5156.snapshot.output_enabled;
}

static void dp_drive_high(void)
{
  board_clkdp_set_mode(BOARD_CLKDP_DRIVE_HIGH);
}

static void dp_drive_low(void)
{
  board_clkdp_set_mode(BOARD_CLKDP_DRIVE_LOW);
}

static void dp_release(void)
{
  board_clkdp_set_mode(BOARD_CLKDP_TRISTATE);
}

static bool i2c_write(
    uint8_t reg_address,
    const uint8_t *data,
    size_t len)
{
  return board_clock_i2c_write(reg_address, data, len) == BOARD_OK;
}

static bool i2c_read(
    uint8_t reg_address,
    uint8_t *data,
    size_t len)
{
  return board_clock_i2c_read(reg_address, data, len) == BOARD_OK;
}
