/**
  ******************************************************************************
  * @file           : dw3000.h
  * @brief          : Project-facing DW3000-family driver interface
  ******************************************************************************
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
  uint32_t (*get_time_ms)(void);
  int32_t (*lock)(void);
  void (*unlock)(int32_t lock_state);
} dw3000_platform_t;

typedef struct {
  uint32_t device_id;
  bool initialized;
  bool configured;
} dw3000_device_t;

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
  bool extended_phr;
  uint8_t tx_pulse_generator_delay;
  uint32_t tx_power;
  uint16_t tx_pulse_generator_count;
  uint16_t tx_antenna_delay;
  uint16_t rx_antenna_delay;
} dw3000_radio_config_t;

typedef struct {
  bool complete;
  uint64_t timestamp;
} dw3000_tx_result_t;

/* Temporary reference-clock diagnostic; remove after board clock validation. */
typedef struct {
  int32_t pll_result;
  int32_t restore_result;
  uint32_t pll_status;
  bool xtal_settled;
  bool pll_locked;
  bool calibration_done;
} dw3000_clock_diagnostic_t;

dw3000_status_t dw3000_init(
    dw3000_device_t *device,
    const dw3000_platform_t *platform);
dw3000_status_t dw3000_read_device_id(
    dw3000_device_t *device,
    uint32_t *device_id);
bool dw3000_is_supported_device_id(uint32_t device_id);
dw3000_status_t dw3000_configure_radio(
    dw3000_device_t *device,
    const dw3000_radio_config_t *config);
dw3000_status_t dw3000_get_system_time(
    const dw3000_device_t *device,
    uint32_t *system_time);
uint32_t dw3000_microseconds_to_device_time(uint32_t time_us);
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
dw3000_status_t dw3000_run_clock_diagnostic(
    const dw3000_device_t *device,
    dw3000_clock_diagnostic_t *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_DW3000_H */
