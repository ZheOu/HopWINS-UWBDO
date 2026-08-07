/**
  ******************************************************************************
  * @file           : dw3000.h
  * @brief          : Project-facing DW3220TR13 driver interface
  ******************************************************************************
  *
  * The project API keeps Qorvo's DW3000 family name because DW3220 uses that
  * register driver. Qorvo types and dwt_* functions remain private to the
  * implementation. The underlying compatibility driver is a singleton, so one
  * MCU firmware instance controls one physical DW3220.
  *
  * Hardware integration requirements:
  * - RSTn must be driven open-drain: pull Low to reset, then release it.
  * - SPI is 8-bit, MSB-first, mode 0. Keep initialization at or below 7 MHz;
  *   spi_set_fast_rate() may select the normal operating rate afterwards.
  * - Each SPI callback owns chip select for the complete header/data/trailer
  *   transaction and returns zero on success or a nonzero platform error.
  * - lock and unlock are optional while polling, but must be supplied together
  *   before enabling a DW3000 interrupt path.
  */

#ifndef HOPWINS_DW3000_H
#define HOPWINS_DW3000_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define DW3000_DEVICE_ID_MASK     UINT32_C(0xFFFFFF0F)
#define DW3000_DEVICE_ID_EXPECTED UINT32_C(0xDECA0302)
#define DW3000_TIMESTAMP_MASK     UINT64_C(0x000000FFFFFFFFFF)
#define DW3000_FRAME_FCS_LEN      2U
#define DW3000_RX_FRAME_MAX_LEN   1023U
#define DW3000_CIR_MAX_SAMPLES    1016U
#define DW3000_STS_CIR_SAMPLES    512U
#define DW3000_CIR_SAMPLE_BYTES   6U

typedef enum {
  DW3000_STATUS_OK = 0,
  DW3000_STATUS_BAD_ARG = -1,
  DW3000_STATUS_PLATFORM_ERROR = -2,
  DW3000_STATUS_SPI_ERROR = -3,
  DW3000_STATUS_UNSUPPORTED_DEVICE = -4,
  DW3000_STATUS_NOT_READY = -5,
  DW3000_STATUS_INITIALIZATION_ERROR = -6,
  DW3000_STATUS_CLOCK_ERROR = -7,
  DW3000_STATUS_CONFIGURATION_ERROR = -8,
  DW3000_STATUS_BUSY = -9,
  DW3000_STATUS_TX_ERROR = -10,
  DW3000_STATUS_TX_LATE = -11,
  DW3000_STATUS_TIMEOUT = -12,
  DW3000_STATUS_RX_ERROR = -13,
  DW3000_STATUS_RX_CRC_ERROR = -14,
  DW3000_STATUS_RX_FRAME_TOO_LONG = -15,
  DW3000_STATUS_CIR_ERROR = -16,
} dw3000_status_t;

typedef enum {
  DW3000_CHANNEL_5 = 5,
  DW3000_CHANNEL_9 = 9,
} dw3000_channel_t;

typedef enum {
  DW3000_DATA_RATE_850K = 0,
  DW3000_DATA_RATE_6M8,
} dw3000_data_rate_t;

typedef enum {
  DW3000_PAC_SIZE_4 = 4,
  DW3000_PAC_SIZE_8 = 8,
  DW3000_PAC_SIZE_16 = 16,
  DW3000_PAC_SIZE_32 = 32,
} dw3000_pac_size_t;

typedef enum {
  DW3000_SFD_IEEE_4A = 0,
  DW3000_SFD_QORVO_8,
  DW3000_SFD_QORVO_16,
  DW3000_SFD_IEEE_4Z,
} dw3000_sfd_type_t;

typedef enum {
  DW3000_STS_OFF = 0,
  DW3000_STS_MODE_1,
  DW3000_STS_MODE_2,
} dw3000_sts_mode_t;

typedef enum {
  DW3000_RF_PORT_NONE = 0,
  DW3000_RF_PORT_1 = 1,
  DW3000_RF_PORT_2 = 2,
} dw3000_rf_port_t;

typedef enum {
  DW3000_RF_MODE_MANUAL_1 = 1,
  DW3000_RF_MODE_MANUAL_2 = 2,
  DW3000_RF_MODE_AUTO_1_2 = 3,
  DW3000_RF_MODE_AUTO_2_1 = 4,
} dw3000_rf_mode_t;

typedef enum {
  DW3000_PDOA_MODE_DISABLED = 0,
  DW3000_PDOA_MODE_1 = 1,
  DW3000_PDOA_MODE_3 = 3,
} dw3000_pdoa_mode_t;

typedef enum {
  DW3000_CIR_SOURCE_IPATOV = 0,
  DW3000_CIR_SOURCE_STS0,
  DW3000_CIR_SOURCE_STS1,
} dw3000_cir_source_t;

typedef struct dw3000_platform {
  int32_t (*hardware_reset)(void);
  int32_t (*spi_set_slow_rate)(void);
  int32_t (*spi_set_fast_rate)(void);
  int32_t (*spi_read)(
      const uint8_t *header,
      uint16_t header_len,
      uint8_t *data,
      uint16_t data_len);
  int32_t (*spi_write)(
      const uint8_t *header,
      uint16_t header_len,
      const uint8_t *data,
      uint16_t data_len,
      const uint8_t *trailer,
      uint16_t trailer_len);
  void (*delay_ms)(uint32_t delay_ms);
  void (*delay_us)(uint32_t delay_us);
  int32_t (*lock)(void);
  void (*unlock)(int32_t lock_state);
} dw3000_platform_t;

/** Device state plus retained diagnostics from radio configuration. */
typedef struct {
  uint32_t device_id;
  int32_t config_sdk_status;
  int32_t config_transport_status;
  uint32_t config_pll_status;
  uint32_t config_system_status_low;
  uint32_t config_system_status_high;
  bool initialized;
  bool configured;
  bool config_idle_rc;
  bool config_xtal_settled;
  bool config_pll_locked;
  bool config_pll_calibration_done;
} dw3000_device_t;

/** Complete PHY, RF path, TX power, and antenna-delay configuration. */
typedef struct {
  dw3000_channel_t channel;
  uint16_t preamble_length;
  dw3000_pac_size_t pac_size;
  uint8_t tx_preamble_code;
  uint8_t rx_preamble_code;
  dw3000_sfd_type_t sfd_type;
  dw3000_data_rate_t data_rate;
  uint16_t sfd_timeout;
  dw3000_sts_mode_t sts_mode;
  uint16_t sts_length;
  bool sts_sdc;
  bool extended_phr;
  uint8_t tx_pulse_generator_delay;
  uint32_t tx_power;
  uint16_t tx_pulse_generator_count;
  uint16_t tx_antenna_delay;
  uint16_t rx_antenna_delay;
  dw3000_rf_mode_t rf_mode;
  dw3000_pdoa_mode_t pdoa_mode;
} dw3000_radio_config_t;

typedef struct {
  bool complete;
  uint64_t timestamp;
} dw3000_tx_result_t;

typedef struct {
  bool complete;
  bool ranging_frame;
  uint16_t frame_len;
  uint64_t timestamp;
  uint64_t raw_timestamp;
  uint32_t system_status;
  int16_t clock_offset;
  int32_t carrier_integrator;
} dw3000_rx_result_t;

typedef struct {
  uint32_t system_time_hi32;
  uint32_t system_status_high;
  uint32_t rx_finfo;
  uint32_t cia_diag_0;
  uint32_t cia_diag_1;
  uint8_t dgc_decision;
} dw3000_rx_register_snapshot_t;

typedef struct {
  uint32_t power;
  uint32_t first_path_amplitude_1;
  uint32_t first_path_amplitude_2;
  uint32_t first_path_amplitude_3;
  uint32_t peak_amplitude;
  uint32_t first_path_threshold;
  uint16_t peak_index;
  uint16_t first_path_index;
  uint16_t accumulated_symbols;
  uint16_t early_first_path_index;
  uint8_t early_first_path_confidence;
  int16_t rssi_q8_8;
  int16_t first_path_power_q8_8;
} dw3000_cir_diagnostic_t;

/** CIA results shared by the Ipatov, STS0, and Mode-3 STS1 estimates. */
typedef struct {
  uint64_t ipatov_timestamp;
  uint64_t sts0_timestamp;
  uint64_t sts1_timestamp;
  int64_t tdoa;
  uint8_t ipatov_status;
  uint16_t sts0_status;
  uint16_t sts1_status;
  uint16_t ipatov_phase;
  uint16_t sts0_phase;
  uint16_t sts1_phase;
  int16_t pdoa;
} dw3000_pdoa_diagnostic_t;

/** Reset, probe, initialize OTP state, and verify communication at fast SPI. */
dw3000_status_t dw3000_init(
    dw3000_device_t *device,
    const dw3000_platform_t *platform);
dw3000_status_t dw3000_read_device_id(
    dw3000_device_t *device,
    uint32_t *device_id);
bool dw3000_is_supported_device_id(uint32_t device_id);

/** Apply one validated radio profile to an initialized device. */
dw3000_status_t dw3000_configure_radio(
    dw3000_device_t *device,
    const dw3000_radio_config_t *config);
dw3000_status_t dw3000_get_system_time(
    const dw3000_device_t *device,
    uint32_t *system_time);
uint32_t dw3000_microseconds_to_device_time(uint32_t time_us);

/* Nonblocking TX operations; completion is reported by dw3000_poll_transmit. */
dw3000_status_t dw3000_transmit_immediate(
    const dw3000_device_t *device,
    const uint8_t *frame,
    uint16_t frame_len);
dw3000_status_t dw3000_transmit_delayed(
    const dw3000_device_t *device,
    const uint8_t *frame,
    uint16_t frame_len,
    uint32_t transmit_time);
dw3000_status_t dw3000_abort_transmit(
    const dw3000_device_t *device);
dw3000_status_t dw3000_poll_transmit(
    const dw3000_device_t *device,
    dw3000_tx_result_t *result);

/* Polling RX operations; metadata and CIR must be read before the next start. */
dw3000_status_t dw3000_receive_start(
    const dw3000_device_t *device);
dw3000_status_t dw3000_abort_receive(
    const dw3000_device_t *device);
dw3000_status_t dw3000_poll_receive(
    const dw3000_device_t *device,
    uint8_t *frame,
    uint16_t frame_capacity,
    dw3000_rx_result_t *result);
dw3000_status_t dw3000_read_rx_register_snapshot(
    const dw3000_device_t *device,
    dw3000_rx_register_snapshot_t *snapshot);
uint16_t dw3000_get_cir_sample_count(
    const dw3000_device_t *device,
    dw3000_cir_source_t source);
dw3000_status_t dw3000_read_cir_diagnostics(
    const dw3000_device_t *device,
    dw3000_cir_source_t source,
    dw3000_cir_diagnostic_t *diagnostic);
dw3000_status_t dw3000_read_cir_48b(
    const dw3000_device_t *device,
    dw3000_cir_source_t source,
    uint16_t sample_offset,
    uint16_t sample_count,
    uint8_t *samples);
dw3000_status_t dw3000_read_pdoa_diagnostics(
    const dw3000_device_t *device,
    dw3000_pdoa_diagnostic_t *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_DW3000_H */
