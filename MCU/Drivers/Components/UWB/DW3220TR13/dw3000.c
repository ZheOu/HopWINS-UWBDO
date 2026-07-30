/**
  ******************************************************************************
  * @file           : dw3000.c
  * @brief          : DW3000-family initialization and Qorvo SDK adaptation
  ******************************************************************************
  */

#include "dw3000.h"

#include "deca_device_api.h"
#include "deca_interface.h"
#include "dw3000_deca_regs.h"

#include <stddef.h>

#define DW3000_PLL_STATUS_XTAL_SETTLED_MASK UINT32_C(0x40)
#define DW3000_PLL_STATUS_LOCK_MASK         UINT32_C(0x02)
#define DW3000_PLL_STATUS_CAL_DONE_MASK     UINT32_C(0x01)
#define DW3000_STANDARD_FRAME_MAX_LEN       127U
#define DW3000_EXTENDED_FRAME_MAX_LEN       1023U
#define DW3000_TX_BUFFER_OFFSET             0U
#define DW3000_STS_DEFAULT_LENGTH           DWT_STS_LEN_64

extern const struct dwt_driver_s dw3000_driver;

static const dw3000_platform_t *s_platform;
static int32_t s_transport_error;
static bool s_sdk_ready;
static bool s_tx_pending;
static bool s_rx_pending;
static bool s_rx_data_ready;
static uint16_t s_max_frame_len;
static uint16_t s_cir_sample_count;

static dw3000_status_t validate_radio_config(
    const dw3000_radio_config_t *config);
static dw3000_status_t prepare_transmit(
    const dw3000_device_t *device,
    const uint8_t *frame,
    uint16_t frame_len);
static bool sts_length_is_valid(uint16_t sts_length);
static dwt_sts_lengths_e sts_length_to_sdk(uint16_t sts_length);
static uint64_t timestamp_from_little_endian(
    const uint8_t *timestamp,
    uint32_t len);

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
         (platform->delay_us != NULL) &&
         (platform->get_time_ms != NULL);
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

dw3000_status_t dw3000_configure_radio(
    dw3000_device_t *device,
    const dw3000_radio_config_t *config)
{
  dwt_config_t sdk_config;
  dwt_txconfig_t tx_config;
  dw3000_status_t status;

  if ((device == NULL) || (config == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !s_sdk_ready || (s_platform == NULL)) {
    return DW3000_STATUS_NOT_READY;
  }

  status = validate_radio_config(config);
  if (status != DW3000_STATUS_OK) {
    return status;
  }

  sdk_config = (dwt_config_t){
    .chan = (uint8_t)config->channel,
    .txPreambLength = config->preamble_length,
    .rxPAC = (config->pac_size == DW3000_PAC_SIZE_4)
                 ? DWT_PAC4
             : (config->pac_size == DW3000_PAC_SIZE_8)
                 ? DWT_PAC8
             : (config->pac_size == DW3000_PAC_SIZE_16)
                 ? DWT_PAC16
                 : DWT_PAC32,
    .txCode = config->tx_preamble_code,
    .rxCode = config->rx_preamble_code,
    .sfdType = (dwt_sfd_type_e)config->sfd_type,
    .dataRate = (config->data_rate == DW3000_DATA_RATE_6M8)
                    ? DWT_BR_6M8
                    : DWT_BR_850K,
    .phrMode = config->extended_phr ? DWT_PHRMODE_EXT : DWT_PHRMODE_STD,
    .phrRate = DWT_PHRRATE_STD,
    .sfdTO = config->sfd_timeout,
    .stsMode = (config->sts_mode == DW3000_STS_MODE_1)
                   ? DWT_STS_MODE_1
               : (config->sts_mode == DW3000_STS_MODE_2)
                   ? DWT_STS_MODE_2
                   : DWT_STS_MODE_OFF,
    .stsLength = (config->sts_mode == DW3000_STS_OFF)
                     ? DW3000_STS_DEFAULT_LENGTH
                     : sts_length_to_sdk(config->sts_length),
    .pdoaMode = DWT_PDOA_M0,
  };
  tx_config = (dwt_txconfig_t){
    .PGdly = config->tx_pulse_generator_delay,
    .power = config->tx_power,
    .PGcount = config->tx_pulse_generator_count,
  };

  s_transport_error = 0;
  device->configured = false;
  s_tx_pending = false;
  s_rx_pending = false;
  s_rx_data_ready = false;

  if (dwt_configure(&sdk_config) != (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_CONFIGURATION_ERROR;
  }

  dwt_configuretxrf(&tx_config);
  dwt_configure_rf_port(
      (config->rf_port == DW3000_RF_PORT_1)
          ? DWT_RF_PORT_MANUAL_1
          : DWT_RF_PORT_MANUAL_2);
  dwt_settxantennadelay(config->tx_antenna_delay);
  dwt_setrxantennadelay(config->rx_antenna_delay);
  if (s_transport_error != 0) {
    return DW3000_STATUS_SPI_ERROR;
  }

  s_max_frame_len = config->extended_phr
                        ? DW3000_EXTENDED_FRAME_MAX_LEN
                        : DW3000_STANDARD_FRAME_MAX_LEN;
  s_cir_sample_count =
      (config->rx_preamble_code >= PCODE_PRF64_START)
          ? DWT_CIR_LEN_IP_PRF64
          : DWT_CIR_LEN_IP_PRF16;
  device->configured = true;
  return DW3000_STATUS_OK;
}

dw3000_status_t dw3000_get_system_time(
    const dw3000_device_t *device,
    uint32_t *system_time)
{
  uint8_t timestamp[sizeof(uint32_t)];

  if ((device == NULL) || (system_time == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !device->configured || !s_sdk_ready) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  dwt_readsystime(timestamp);
  if (s_transport_error != 0) {
    return DW3000_STATUS_SPI_ERROR;
  }

  *system_time = (uint32_t)timestamp_from_little_endian(
      timestamp,
      (uint32_t)sizeof(timestamp));
  return DW3000_STATUS_OK;
}

uint32_t dw3000_microseconds_to_device_time(uint32_t time_us)
{
  return US_TO_DTU(time_us);
}

dw3000_status_t dw3000_transmit_immediate(
    const dw3000_device_t *device,
    const uint8_t *frame,
    uint16_t frame_len)
{
  dw3000_status_t status = prepare_transmit(device, frame, frame_len);

  if (status != DW3000_STATUS_OK) {
    return status;
  }

  if (dwt_starttx((uint8_t)DWT_START_TX_IMMEDIATE) !=
      (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_TX_ERROR;
  }

  s_tx_pending = true;
  return DW3000_STATUS_OK;
}

dw3000_status_t dw3000_transmit_delayed(
    const dw3000_device_t *device,
    const uint8_t *frame,
    uint16_t frame_len,
    uint32_t transmit_time)
{
  dw3000_status_t status = prepare_transmit(device, frame, frame_len);

  if (status != DW3000_STATUS_OK) {
    return status;
  }

  dwt_setdelayedtrxtime(transmit_time);
  if (s_transport_error != 0) {
    return DW3000_STATUS_SPI_ERROR;
  }

  if (dwt_starttx((uint8_t)DWT_START_TX_DELAYED) !=
      (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_TX_LATE;
  }

  s_tx_pending = true;
  return DW3000_STATUS_OK;
}

dw3000_status_t dw3000_abort_transmit(
    const dw3000_device_t *device)
{
  if (device == NULL) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !device->configured || !s_sdk_ready) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  dwt_forcetrxoff();
  dwt_writesysstatuslo(
      DWT_INT_TXFRS_BIT_MASK | DWT_INT_HPDWARN_BIT_MASK);
  s_tx_pending = false;
  return (s_transport_error == 0)
             ? DW3000_STATUS_OK
             : DW3000_STATUS_SPI_ERROR;
}

dw3000_status_t dw3000_poll_transmit(
    const dw3000_device_t *device,
    dw3000_tx_result_t *result)
{
  uint8_t timestamp[5];
  uint32_t system_status;

  if ((device == NULL) || (result == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }

  result->complete = false;
  result->timestamp = 0U;

  if (!device->initialized || !device->configured || !s_sdk_ready) {
    return DW3000_STATUS_NOT_READY;
  }
  if (!s_tx_pending) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  system_status = dwt_readsysstatuslo();
  if (s_transport_error != 0) {
    s_tx_pending = false;
    return DW3000_STATUS_SPI_ERROR;
  }
  if ((system_status & DWT_INT_TXFRS_BIT_MASK) == 0U) {
    return DW3000_STATUS_OK;
  }

  dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
  dwt_readtxtimestamp(timestamp);
  s_tx_pending = false;
  if (s_transport_error != 0) {
    return DW3000_STATUS_SPI_ERROR;
  }

  result->timestamp =
      timestamp_from_little_endian(timestamp, (uint32_t)sizeof(timestamp)) &
      DW3000_TIMESTAMP_MASK;
  result->complete = true;
  return DW3000_STATUS_OK;
}

dw3000_status_t dw3000_receive_start(
    const dw3000_device_t *device)
{
  uint32_t clear_mask =
      SYS_STATUS_ALL_RX_GOOD |
      SYS_STATUS_ALL_RX_ERR |
      SYS_STATUS_ALL_RX_TO;

  if (device == NULL) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !device->configured || !s_sdk_ready) {
    return DW3000_STATUS_NOT_READY;
  }
  if (s_tx_pending || s_rx_pending) {
    return DW3000_STATUS_BUSY;
  }

  s_transport_error = 0;
  s_rx_data_ready = false;
  dwt_forcetrxoff();
  dwt_writesysstatuslo(clear_mask);
  dwt_configciadiag((uint8_t)DW_CIA_DIAG_LOG_ALL);
  dwt_setrxtimeout(0U);
  dwt_setpreambledetecttimeout(0U);
  if (dwt_rxenable((int32_t)DWT_START_RX_IMMEDIATE) !=
      (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_RX_ERROR;
  }
  if (s_transport_error != 0) {
    return DW3000_STATUS_SPI_ERROR;
  }

  s_rx_pending = true;
  return DW3000_STATUS_OK;
}

dw3000_status_t dw3000_abort_receive(
    const dw3000_device_t *device)
{
  uint32_t clear_mask =
      SYS_STATUS_ALL_RX_GOOD |
      SYS_STATUS_ALL_RX_ERR |
      SYS_STATUS_ALL_RX_TO;

  if (device == NULL) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !device->configured || !s_sdk_ready) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  dwt_forcetrxoff();
  dwt_writesysstatuslo(clear_mask);
  s_rx_pending = false;
  s_rx_data_ready = false;
  return (s_transport_error == 0)
             ? DW3000_STATUS_OK
             : DW3000_STATUS_SPI_ERROR;
}

dw3000_status_t dw3000_poll_receive(
    const dw3000_device_t *device,
    uint8_t *frame,
    uint16_t frame_capacity,
    dw3000_rx_result_t *result)
{
  uint8_t ranging_flags = 0U;
  uint8_t timestamp[5];
  uint8_t raw_timestamp[5];
  uint32_t system_status;
  uint32_t error_mask = SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO;
  uint16_t frame_len;

  if ((device == NULL) || (frame == NULL) || (frame_capacity == 0U) ||
      (result == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }

  *result = (dw3000_rx_result_t){0};

  if (!device->initialized || !device->configured || !s_sdk_ready) {
    return DW3000_STATUS_NOT_READY;
  }
  if (!s_rx_pending) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  system_status = dwt_readsysstatuslo();
  result->system_status = system_status;
  if (s_transport_error != 0) {
    s_rx_pending = false;
    return DW3000_STATUS_SPI_ERROR;
  }

  if ((system_status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
    frame_len = dwt_getframelength(&ranging_flags);
    if ((frame_len == 0U) || (frame_len > frame_capacity) ||
        (frame_len > s_max_frame_len)) {
      dwt_forcetrxoff();
      dwt_writesysstatuslo(
          SYS_STATUS_ALL_RX_GOOD |
          SYS_STATUS_ALL_RX_ERR |
          SYS_STATUS_ALL_RX_TO);
      s_rx_pending = false;
      return DW3000_STATUS_RX_FRAME_TOO_LONG;
    }

    dwt_readrxdata(frame, frame_len, 0U);
    dwt_readrxtimestamp(timestamp, DWT_COMPAT_NONE);
    dwt_readrxtimestampunadj(raw_timestamp);
    result->clock_offset = dwt_readclockoffset();
    result->carrier_integrator = dwt_readcarrierintegrator();
    if (s_transport_error != 0) {
      s_rx_pending = false;
      return DW3000_STATUS_SPI_ERROR;
    }

    result->complete = true;
    result->ranging_frame =
        (ranging_flags & DWT_CB_DATA_RX_FLAG_RNG) != 0U;
    result->frame_len = frame_len;
    result->timestamp =
        timestamp_from_little_endian(timestamp, (uint32_t)sizeof(timestamp)) &
        DW3000_TIMESTAMP_MASK;
    result->raw_timestamp =
        timestamp_from_little_endian(
            raw_timestamp,
            (uint32_t)sizeof(raw_timestamp)) &
        DW3000_TIMESTAMP_MASK;
    s_rx_pending = false;
    s_rx_data_ready = true;
    return DW3000_STATUS_OK;
  }

  if ((system_status & error_mask) != 0U) {
    dwt_forcetrxoff();
    dwt_writesysstatuslo(error_mask);
    s_rx_pending = false;
    s_rx_data_ready = false;
    if (s_transport_error != 0) {
      return DW3000_STATUS_SPI_ERROR;
    }
    if ((system_status & DWT_INT_RXFCE_BIT_MASK) != 0U) {
      return DW3000_STATUS_RX_CRC_ERROR;
    }
    if ((system_status & SYS_STATUS_ALL_RX_TO) != 0U) {
      return DW3000_STATUS_TIMEOUT;
    }
    return DW3000_STATUS_RX_ERROR;
  }

  return DW3000_STATUS_OK;
}

dw3000_status_t dw3000_read_rx_register_snapshot(
    const dw3000_device_t *device,
    dw3000_rx_register_snapshot_t *snapshot)
{
  if ((device == NULL) || (snapshot == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !device->configured || !s_sdk_ready ||
      !s_rx_data_ready) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  *snapshot = (dw3000_rx_register_snapshot_t){
    .system_time_hi32 = dwt_readsystimestamphi32(),
    .system_status_high = dwt_readsysstatushi(),
    .rx_finfo = dwt_read_reg(RX_FINFO_ID),
    .cia_diag_0 = dwt_read_reg(CIA_DIAG_0_ID),
    .cia_diag_1 = dwt_read_reg(CIA_DIAG_1_ID),
    .dgc_decision = dwt_get_dgcdecision(),
  };

  return (s_transport_error == 0)
             ? DW3000_STATUS_OK
             : DW3000_STATUS_SPI_ERROR;
}

uint16_t dw3000_get_cir_sample_count(
    const dw3000_device_t *device)
{
  if ((device == NULL) || !device->initialized || !device->configured ||
      !s_sdk_ready) {
    return 0U;
  }

  return s_cir_sample_count;
}

dw3000_status_t dw3000_read_cir_diagnostics(
    const dw3000_device_t *device,
    dw3000_cir_diagnostic_t *diagnostic)
{
  dwt_cirdiags_t sdk_diagnostic = {0};

  if ((device == NULL) || (diagnostic == NULL)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !device->configured || !s_sdk_ready ||
      !s_rx_data_ready) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  if (dwt_readdiagnostics_acc(&sdk_diagnostic, DWT_ACC_IDX_IP_M) !=
      (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_CIR_ERROR;
  }

  *diagnostic = (dw3000_cir_diagnostic_t){
    .power = sdk_diagnostic.power,
    .first_path_amplitude_1 = sdk_diagnostic.F1,
    .first_path_amplitude_2 = sdk_diagnostic.F2,
    .first_path_amplitude_3 = sdk_diagnostic.F3,
    .peak_amplitude = sdk_diagnostic.peakAmp,
    .first_path_threshold = sdk_diagnostic.FpThreshold,
    .peak_index = sdk_diagnostic.peakIndex,
    .first_path_index = sdk_diagnostic.FpIndex,
    .accumulated_symbols = sdk_diagnostic.accumCount,
    .early_first_path_index = sdk_diagnostic.EFpIndex,
    .early_first_path_confidence = sdk_diagnostic.EFpConfLevel,
    .rssi_q8_8 = INT16_MIN,
    .first_path_power_q8_8 = INT16_MIN,
  };

  (void)dwt_calculate_rssi(
      &sdk_diagnostic,
      DWT_ACC_IDX_IP_M,
      &diagnostic->rssi_q8_8);
  (void)dwt_calculate_first_path_power(
      &sdk_diagnostic,
      DWT_ACC_IDX_IP_M,
      &diagnostic->first_path_power_q8_8);

  return (s_transport_error == 0)
             ? DW3000_STATUS_OK
             : DW3000_STATUS_SPI_ERROR;
}

dw3000_status_t dw3000_read_cir_48b(
    const dw3000_device_t *device,
    uint16_t sample_offset,
    uint16_t sample_count,
    uint8_t *samples)
{
  uint32_t sample_end = (uint32_t)sample_offset + sample_count;

  if ((device == NULL) || (samples == NULL) || (sample_count == 0U) ||
      (sample_end > s_cir_sample_count)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !device->configured || !s_sdk_ready ||
      !s_rx_data_ready) {
    return DW3000_STATUS_NOT_READY;
  }

  s_transport_error = 0;
  if (dwt_readcir_48b(
          samples,
          DWT_ACC_IDX_IP_M,
          sample_offset,
          sample_count) != (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_CIR_ERROR;
  }

  return (s_transport_error == 0)
             ? DW3000_STATUS_OK
             : DW3000_STATUS_SPI_ERROR;
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
  device->configured = false;
  s_platform = platform;
  s_transport_error = 0;
  s_sdk_ready = false;
  s_tx_pending = false;
  s_rx_pending = false;
  s_rx_data_ready = false;
  s_max_frame_len = 0U;
  s_cir_sample_count = 0U;

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

static dw3000_status_t validate_radio_config(
    const dw3000_radio_config_t *config)
{
  bool channel_valid;
  bool pac_valid;
  bool rate_valid;
  bool sfd_valid;
  bool sts_valid;

  channel_valid =
      (config->channel == DW3000_CHANNEL_5) ||
      (config->channel == DW3000_CHANNEL_9);
  pac_valid =
      (config->pac_size == DW3000_PAC_SIZE_4) ||
      (config->pac_size == DW3000_PAC_SIZE_8) ||
      (config->pac_size == DW3000_PAC_SIZE_16) ||
      (config->pac_size == DW3000_PAC_SIZE_32);
  rate_valid =
      (config->data_rate == DW3000_DATA_RATE_850K) ||
      (config->data_rate == DW3000_DATA_RATE_6M8);
  sfd_valid =
      (config->sfd_type <= DW3000_SFD_IEEE_4Z);
  sts_valid =
      (config->sts_mode <= DW3000_STS_MODE_2);

  if (!channel_valid || !pac_valid || !rate_valid || !sfd_valid ||
      !sts_valid ||
      !CHECK_PREAMBLE_LEN_VALIDITY(config->preamble_length) ||
      (config->tx_preamble_code == 0U) ||
      (config->tx_preamble_code > 24U) ||
      (config->rx_preamble_code == 0U) ||
      (config->rx_preamble_code > 24U) ||
      (config->sfd_timeout == 0U) ||
      (config->tx_pulse_generator_delay > 0x3FU) ||
      ((config->rf_port != DW3000_RF_PORT_1) &&
       (config->rf_port != DW3000_RF_PORT_2)) ||
      ((config->sts_mode != DW3000_STS_OFF) &&
       !sts_length_is_valid(config->sts_length))) {
    return DW3000_STATUS_BAD_ARG;
  }

  return DW3000_STATUS_OK;
}

static bool sts_length_is_valid(uint16_t sts_length)
{
  switch (sts_length) {
    case 16U:
    case 32U:
    case 64U:
    case 128U:
    case 256U:
    case 512U:
    case 1024U:
    case 2048U:
      return true;
    default:
      return false;
  }
}

static dwt_sts_lengths_e sts_length_to_sdk(uint16_t sts_length)
{
  switch (sts_length) {
    case 16U:
      return DWT_STS_LEN_16;
    case 32U:
      return DWT_STS_LEN_32;
    case 128U:
      return DWT_STS_LEN_128;
    case 256U:
      return DWT_STS_LEN_256;
    case 512U:
      return DWT_STS_LEN_512;
    case 1024U:
      return DWT_STS_LEN_1024;
    case 2048U:
      return DWT_STS_LEN_2048;
    case 64U:
    default:
      return DWT_STS_LEN_64;
  }
}

static dw3000_status_t prepare_transmit(
    const dw3000_device_t *device,
    const uint8_t *frame,
    uint16_t frame_len)
{
  uint16_t over_air_len;

  if ((device == NULL) || (frame == NULL) || (frame_len == 0U)) {
    return DW3000_STATUS_BAD_ARG;
  }
  if (!device->initialized || !device->configured || !s_sdk_ready) {
    return DW3000_STATUS_NOT_READY;
  }
  if (s_tx_pending || s_rx_pending || s_rx_data_ready) {
    return DW3000_STATUS_BUSY;
  }

  over_air_len = frame_len + DW3000_FRAME_FCS_LEN;
  if ((over_air_len < frame_len) || (over_air_len > s_max_frame_len)) {
    return DW3000_STATUS_BAD_ARG;
  }

  s_transport_error = 0;
  dwt_writesysstatuslo(
      DWT_INT_TXFRS_BIT_MASK | DWT_INT_HPDWARN_BIT_MASK);
  if (dwt_writetxdata(
          frame_len,
          (uint8_t *)(uintptr_t)frame,
          DW3000_TX_BUFFER_OFFSET) != (int32_t)DWT_SUCCESS) {
    return (s_transport_error != 0)
               ? DW3000_STATUS_SPI_ERROR
               : DW3000_STATUS_TX_ERROR;
  }

  dwt_writetxfctrl(
      over_air_len,
      DW3000_TX_BUFFER_OFFSET,
      0U);
  return (s_transport_error == 0)
             ? DW3000_STATUS_OK
             : DW3000_STATUS_SPI_ERROR;
}

static uint64_t timestamp_from_little_endian(
    const uint8_t *timestamp,
    uint32_t len)
{
  uint64_t value = 0U;

  for (uint32_t i = 0U; i < len; i++) {
    value |= (uint64_t)timestamp[i] << (8U * i);
  }

  return value;
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
