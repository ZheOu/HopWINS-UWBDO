/**
  ******************************************************************************
  * @file           : uwb_service.h
  * @brief          : DW3000 initialization, diagnostics, and TX service
  ******************************************************************************
  */

#ifndef HOPWINS_UWB_SERVICE_H
#define HOPWINS_UWB_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dw3000.h"
#include "uwb_profile.h"

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

typedef struct {
  dw3000_device_t device;
  dw3000_status_t init_status;
  dw3000_status_t config_status;
  dw3000_clock_diagnostic_t clock_diagnostic;
  dw3000_status_t clock_status;
  dw3000_radio_config_t radio_config;
  uint32_t transmit_interval_us;
  uint32_t sent_count;
  uint32_t error_count;
  uint32_t late_count;
  bool periodic_tx_enabled;
  bool transmit_pending;
  bool clock_diagnostic_run;
} uwb_service_state_t;

dw3000_status_t uwb_service_init(const dw3000_platform_t *platform);
dw3000_status_t uwb_service_run_clock_diagnostic(void);
dw3000_status_t uwb_service_start_periodic_transmit(
    const uwb_profile_t *profile);
void uwb_service_stop_periodic_transmit(void);
void uwb_service_process(void);
bool uwb_service_take_tx_event(uwb_service_tx_event_t *event);
const uwb_service_state_t *uwb_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_UWB_SERVICE_H */
