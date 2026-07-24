/**
  ******************************************************************************
  * @file           : dw3000.c
  * @brief          : DW3000-family initialization and Qorvo SDK adaptation
  ******************************************************************************
  */

#include "dw3000.h"

#include "deca_device_api.h"
#include "deca_interface.h"

#include <stddef.h>

#define DW3000_PLL_STATUS_XTAL_SETTLED_MASK UINT32_C(0x40)
#define DW3000_PLL_STATUS_LOCK_MASK         UINT32_C(0x02)
#define DW3000_PLL_STATUS_CAL_DONE_MASK     UINT32_C(0x01)

extern const struct dwt_driver_s dw3000_driver;

static const dw3000_platform_t *s_platform;
static int32_t s_transport_error;
static bool s_sdk_ready;

static void record_transport_status(int32_t status)
{
  if ((status != 0) && (s_transport_error == 0)) {
    s_transport_error = status;
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
    record_transport_status(-1);
    return (int32_t)DWT_ERROR;
  }

  status = s_platform->spi_read(header, header_len, data, data_len);
  record_transport_status(status);
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
    record_transport_status(-1);
    return (int32_t)DWT_ERROR;
  }

  status = s_platform->spi_write(
      header,
      header_len,
      data,
      data_len,
      NULL,
      0U);
  record_transport_status(status);
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
    record_transport_status(-1);
    return (int32_t)DWT_ERROR;
  }

  status = s_platform->spi_write(
      header,
      header_len,
      data,
      data_len,
      &crc,
      1U);
  record_transport_status(status);
  return (status == 0) ? (int32_t)DWT_SUCCESS : (int32_t)DWT_ERROR;
}

static void sdk_spi_set_slow_rate(void)
{
  if ((s_platform == NULL) || (s_platform->spi_set_slow_rate == NULL)) {
    record_transport_status(-1);
    return;
  }

  record_transport_status(s_platform->spi_set_slow_rate());
}

static void sdk_spi_set_fast_rate(void)
{
  if ((s_platform == NULL) || (s_platform->spi_set_fast_rate == NULL)) {
    record_transport_status(-1);
    return;
  }

  record_transport_status(s_platform->spi_set_fast_rate());
}

static void sdk_probe_wakeup(void)
{
  /*
   * dw3000_init() performs a full hardware reset immediately before probing,
   * so the device is already awake here.
   */
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

static bool platform_is_valid(const dw3000_platform_t *platform)
{
  return (platform != NULL) &&
         (platform->hardware_reset != NULL) &&
         (platform->spi_set_slow_rate != NULL) &&
         (platform->spi_set_fast_rate != NULL) &&
         (platform->spi_read != NULL) &&
         (platform->spi_write != NULL) &&
         (platform->delay_ms != NULL) &&
         (platform->delay_us != NULL);
}

static dw3000_status_t platform_read_device_id(
    dw3000_device_t *device,
    uint32_t *device_id)
{
  uint8_t header = 0U;
  uint8_t data[sizeof(uint32_t)];
  int32_t status;

  status = s_platform->spi_read(
      &header,
      (uint16_t)sizeof(header),
      data,
      (uint16_t)sizeof(data));
  if (status != 0) {
    record_transport_status(status);
    return DW3000_STATUS_SPI_ERROR;
  }

  *device_id =
      ((uint32_t)data[3] << 24U) |
      ((uint32_t)data[2] << 16U) |
      ((uint32_t)data[1] << 8U) |
      (uint32_t)data[0];
  device->device_id = *device_id;

  return dw3000_is_supported_device_id(*device_id)
             ? DW3000_STATUS_OK
             : DW3000_STATUS_UNSUPPORTED_DEVICE;
}

bool dw3000_is_supported_device_id(uint32_t device_id)
{
  return (device_id & DW3000_DEVICE_ID_MASK) ==
         (DW3000_DEVICE_ID_EXPECTED & DW3000_DEVICE_ID_MASK);
}

dw3000_status_t dw3000_read_device_id(
    dw3000_device_t *device,
    uint32_t *device_id)
{
  uint32_t value;

  if ((device == NULL) || (device_id == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if ((s_platform == NULL) || !s_sdk_ready) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  value = dwt_readdevid();
  if (s_transport_error != 0) {
    return DW3000_STATUS_SPI_ERROR;
  }

  device->device_id = value;
  *device_id = value;
  return dw3000_is_supported_device_id(value)
             ? DW3000_STATUS_OK
             : DW3000_STATUS_UNSUPPORTED_DEVICE;
}

dw3000_status_t dw3000_init(
    dw3000_device_t *device,
    const dw3000_platform_t *platform)
{
  dw3000_status_t status;
  uint32_t device_id;

  if ((device == NULL) || !platform_is_valid(platform)) {
    return DW3000_STATUS_BAD_ARG;
  }

  device->device_id = 0U;
  device->initialized = false;
  s_platform = platform;
  s_transport_error = 0;
  s_sdk_ready = false;

  if (s_platform->spi_set_slow_rate() != 0) {
    return DW3000_STATUS_PLATFORM_ERROR;
  }

  if (s_platform->hardware_reset() != 0) {
    return DW3000_STATUS_PLATFORM_ERROR;
  }

  status = platform_read_device_id(device, &device_id);
  if (status != DW3000_STATUS_OK) {
    return status;
  }

  if (dwt_probe(&s_probe) != (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_UNSUPPORTED_DEVICE;
  }
  s_sdk_ready = true;

  status = dw3000_read_device_id(device, &device_id);
  if (status != DW3000_STATUS_OK) {
    return status;
  }

  if (dwt_checkidlerc() == 0U) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  if (dwt_initialise(DWT_READ_OTP_ALL) != (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_INITIALIZATION_ERROR;
  }

  if (s_transport_error != 0) {
    return DW3000_STATUS_SPI_ERROR;
  }

  if (s_platform->spi_set_fast_rate() != 0) {
    return DW3000_STATUS_PLATFORM_ERROR;
  }

  status = dw3000_read_device_id(device, &device_id);
  if (status != DW3000_STATUS_OK) {
    return status;
  }

  device->initialized = true;
  return DW3000_STATUS_OK;
}

dw3000_status_t dw3000_run_clock_diagnostic(
    const dw3000_device_t *device,
    dw3000_clock_diagnostic_t *diagnostic)
{
  bool clock_ok;

  if ((device == NULL) || (diagnostic == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }

  diagnostic->pll_result = (int32_t)DWT_ERROR;
  diagnostic->restore_result = (int32_t)DWT_ERROR;
  diagnostic->pll_status = 0U;
  diagnostic->xtal_settled = false;
  diagnostic->pll_locked = false;
  diagnostic->calibration_done = false;

  if (!device->initialized || !s_sdk_ready || (s_platform == NULL)) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;

  /*
   * dwt_setchannel() programs the selected RF PLL, runs calibration, and
   * attempts to enter IDLE_PLL. This tests more than IDLE_RC SPI access.
   */
  diagnostic->pll_result = dwt_setchannel(DWT_CH5);
  diagnostic->pll_status = dwt_readpllstatus();
  diagnostic->xtal_settled =
      (diagnostic->pll_status & DW3000_PLL_STATUS_XTAL_SETTLED_MASK) != 0U;
  diagnostic->pll_locked =
      (diagnostic->pll_status & DW3000_PLL_STATUS_LOCK_MASK) != 0U;
  diagnostic->calibration_done =
      (diagnostic->pll_status & DW3000_PLL_STATUS_CAL_DONE_MASK) != 0U;

  diagnostic->restore_result = dwt_setdwstate(DWT_DW_IDLE_RC);

  if (s_transport_error != 0) {
    return DW3000_STATUS_SPI_ERROR;
  }

  clock_ok =
      (diagnostic->pll_result == (int32_t)DWT_SUCCESS) &&
      (diagnostic->restore_result == (int32_t)DWT_SUCCESS) &&
      diagnostic->xtal_settled &&
      diagnostic->pll_locked;

  return clock_ok ? DW3000_STATUS_OK : DW3000_STATUS_CLOCK_ERROR;
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
