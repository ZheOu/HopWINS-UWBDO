/**
  ******************************************************************************
  * @file           : dw3000_qorvo_adapter.c
  * @brief          : Internal bridge between the project platform and Qorvo SDK
  ******************************************************************************
  */

#include "dw3000_qorvo_adapter.h"

#include "deca_device_api.h"
#include "deca_interface.h"

#include <stddef.h>

extern const struct dwt_driver_s dw3000_driver;

static const dw3000_platform_t *s_platform;
static dw3000_transport_status_callback_t s_status_callback;

static void report_status(int32_t status)
{
  if ((status != 0) && (s_status_callback != NULL)) {
    s_status_callback(status);
  }
}

static int32_t sdk_spi_read(
    uint16_t header_len,
    uint8_t *header,
    uint16_t data_len,
    uint8_t *data)
{
  int32_t status;

  if ((s_platform == NULL) || (s_platform->spi_read == NULL)) {
    report_status(-1);
    return (int32_t)DWT_ERROR;
  }

  status = s_platform->spi_read(header, header_len, data, data_len);
  report_status(status);
  return (status == 0) ? (int32_t)DWT_SUCCESS : (int32_t)DWT_ERROR;
}

static int32_t sdk_spi_write(
    uint16_t header_len,
    const uint8_t *header,
    uint16_t data_len,
    const uint8_t *data)
{
  int32_t status;

  if ((s_platform == NULL) || (s_platform->spi_write == NULL)) {
    report_status(-1);
    return (int32_t)DWT_ERROR;
  }

  status = s_platform->spi_write(
      header,
      header_len,
      data,
      data_len,
      NULL,
      0U);
  report_status(status);
  return (status == 0) ? (int32_t)DWT_SUCCESS : (int32_t)DWT_ERROR;
}

static int32_t sdk_spi_write_with_crc(
    uint16_t header_len,
    const uint8_t *header,
    uint16_t data_len,
    const uint8_t *data,
    uint8_t crc)
{
  int32_t status;

  if ((s_platform == NULL) || (s_platform->spi_write == NULL)) {
    report_status(-1);
    return (int32_t)DWT_ERROR;
  }

  status = s_platform->spi_write(
      header,
      header_len,
      data,
      data_len,
      &crc,
      1U);
  report_status(status);
  return (status == 0) ? (int32_t)DWT_SUCCESS : (int32_t)DWT_ERROR;
}

static void sdk_spi_set_slow_rate(void)
{
  if ((s_platform == NULL) || (s_platform->spi_set_slow_rate == NULL)) {
    report_status(-1);
    return;
  }

  report_status(s_platform->spi_set_slow_rate());
}

static void sdk_spi_set_fast_rate(void)
{
  if ((s_platform == NULL) || (s_platform->spi_set_fast_rate == NULL)) {
    report_status(-1);
    return;
  }

  report_status(s_platform->spi_set_fast_rate());
}

static void sdk_probe_wakeup(void)
{
  /* dw3000_init() resets and wakes the device immediately before probing. */
}

static const struct dwt_spi_s s_sdk_spi = {
  .readfromspi = sdk_spi_read,
  .writetospi = sdk_spi_write,
  .writetospiwithcrc = sdk_spi_write_with_crc,
  .setslowrate = sdk_spi_set_slow_rate,
  .setfastrate = sdk_spi_set_fast_rate,
};

static struct dwt_driver_s *s_driver_list[] = {
  (struct dwt_driver_s *)&dw3000_driver,
};

static struct dwt_probe_s s_probe = {
  .dw = NULL,
  .spi = (void *)&s_sdk_spi,
  .wakeup_device_with_io = sdk_probe_wakeup,
  .driver_list = s_driver_list,
  .dw_driver_num = 1U,
};

void dw3000_qorvo_adapter_bind(
    const dw3000_platform_t *platform,
    dw3000_transport_status_callback_t status_callback)
{
  s_platform = platform;
  s_status_callback = status_callback;
}

int32_t dw3000_qorvo_adapter_probe(void)
{
  return dwt_probe(&s_probe);
}

void deca_sleep(unsigned int time_ms)
{
  if ((s_platform != NULL) && (s_platform->delay_ms != NULL)) {
    s_platform->delay_ms((uint32_t)time_ms);
  }
}

void deca_usleep(unsigned long time_us)
{
  if ((s_platform != NULL) && (s_platform->delay_us != NULL)) {
    s_platform->delay_us((uint32_t)time_us);
  }
}

decaIrqStatus_t decamutexon(void)
{
  if ((s_platform != NULL) && (s_platform->lock != NULL)) {
    return (decaIrqStatus_t)s_platform->lock();
  }

  return (decaIrqStatus_t)0;
}

void decamutexoff(decaIrqStatus_t lock_state)
{
  if ((s_platform != NULL) && (s_platform->unlock != NULL)) {
    s_platform->unlock((int32_t)lock_state);
  }
}
