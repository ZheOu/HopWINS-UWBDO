/**
  ******************************************************************************
  * @file           : uwb_service.h
  * @brief          : DW3000 initialization, TX, RX, and CIR service
  ******************************************************************************
  */

#ifndef HOPWINS_UWB_SERVICE_H
#define HOPWINS_UWB_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dw3000.h"
#include "uwb_profile.h"

/** Service-owned clocks; neither callback is part of the DW3000 device API. */
typedef struct {
  uint32_t (*get_monotonic_time_ms)(void);
  bool (*get_reference_time_ms)(uint32_t *timestamp_ms);
} uwb_service_time_source_t;

typedef enum {
  UWB_SERVICE_TX_EVENT_NONE = 0,
  UWB_SERVICE_TX_EVENT_COMPLETE,
  UWB_SERVICE_TX_EVENT_ERROR,
} uwb_service_tx_event_type_t;

typedef struct {
  uwb_service_tx_event_type_t type;
  dw3000_status_t status;
  uint32_t sequence;
  uint16_t frame_len;
  uint32_t scheduled_time;
  uint64_t transmit_timestamp;
  uint32_t late_count;
} uwb_service_tx_event_t;

typedef enum {
  UWB_SERVICE_MODE_IDLE = 0,
  UWB_SERVICE_MODE_PERIODIC_TX,
  UWB_SERVICE_MODE_CONTINUOUS_RX,
} uwb_service_mode_t;

typedef enum {
  UWB_SERVICE_CIR_CAPTURE_IPATOV = 0,
  UWB_SERVICE_CIR_CAPTURE_STS_DUAL,
} uwb_service_cir_capture_mode_t;

typedef struct {
  uint16_t sample_count;
  uint16_t pre_first_path_samples;
  uwb_service_cir_capture_mode_t mode;
  bool capture_cir;
} uwb_service_cir_config_t;

typedef struct {
  uint32_t capture_id;
  const uint8_t *frame;
  uint16_t frame_len;
  uint32_t mcu_system_time_ms;
  uint32_t reference_time_ms;
  bool reference_time_valid;
  uint64_t receive_timestamp;
  uint64_t raw_receive_timestamp;
  uint32_t system_status;
  dw3000_rx_register_snapshot_t register_snapshot;
  dw3000_status_t register_status;
  int16_t clock_offset;
  int32_t carrier_integrator;
  bool ranging_frame;
  dw3000_cir_diagnostic_t diagnostic;
  dw3000_status_t diagnostic_status;
  const uint8_t *cir_data;
  uint32_t cir_data_len;
  uint16_t cir_sample_offset;
  uint16_t cir_sample_count;
  uint8_t cir_sample_bytes;
  dw3000_status_t cir_status;
  dw3000_cir_source_t cir_source;
  uint8_t cir_group_size;
  dw3000_rf_port_t rf_port;
  uint16_t rx_antenna_delay;
  dw3000_pdoa_diagnostic_t pdoa_diagnostic;
  dw3000_status_t pdoa_diagnostic_status;
} uwb_service_cir_capture_t;

typedef struct {
  dw3000_device_t device;
  dw3000_status_t init_status;
  dw3000_status_t config_status;
  dw3000_radio_config_t radio_config;
  uint32_t transmit_interval_us;
  uint32_t sent_count;
  uint32_t error_count;
  uint32_t late_count;
  uint32_t received_count;
  uint32_t receive_error_count;
  uint32_t receive_crc_error_count;
  uint32_t receive_recovery_count;
  uint32_t receive_watchdog_count;
  uint32_t capture_queue_full_count;
  uint16_t cir_total_samples;
  uint16_t cir_capture_samples;
  uint8_t queued_capture_count;
  uwb_service_mode_t mode;
  bool cir_capture_enabled;
  bool periodic_tx_enabled;
  bool transmit_pending;
  bool cir_receive_enabled;
  bool receive_pending;
  bool cir_capture_ready;
} uwb_service_state_t;

dw3000_status_t uwb_service_init(
    const dw3000_platform_t *device_platform,
    const uwb_service_time_source_t *time_source);
dw3000_status_t uwb_service_start_periodic_transmit(
    const uwb_profile_t *profile);
void uwb_service_stop_periodic_transmit(void);
dw3000_status_t uwb_service_start_cir_receive(
    const uwb_profile_t *profile,
    const uwb_service_cir_config_t *config);
void uwb_service_stop_cir_receive(void);
const uwb_service_cir_capture_t *uwb_service_get_cir_capture(void);
dw3000_status_t uwb_service_release_cir_capture(void);
void uwb_service_process(void);
bool uwb_service_take_tx_event(uwb_service_tx_event_t *event);
const uwb_service_state_t *uwb_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_UWB_SERVICE_H */
