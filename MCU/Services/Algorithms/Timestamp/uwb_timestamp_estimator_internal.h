/**
  ******************************************************************************
  * @file           : uwb_timestamp_estimator_internal.h
  * @brief          : Internal timestamp estimator strategy contract
  ******************************************************************************
  */

#ifndef HOPWINS_UWB_TIMESTAMP_ESTIMATOR_INTERNAL_H
#define HOPWINS_UWB_TIMESTAMP_ESTIMATOR_INTERNAL_H

#include "uwb_timestamp_estimator.h"

typedef struct {
  uwb_timestamp_estimator_id_t id;
  const char *name;
  bool requires_cir;
  void (*reset)(void);
  uwb_timestamp_result_status_t (*process)(
      const uwb_service_cir_capture_t *capture,
      uwb_timestamp_result_t *result);
} uwb_timestamp_estimator_strategy_t;

extern const uwb_timestamp_estimator_strategy_t
    g_uwb_timestamp_dw_adjusted_strategy;
extern const uwb_timestamp_estimator_strategy_t
    g_uwb_timestamp_dw_raw_strategy;

#endif /* HOPWINS_UWB_TIMESTAMP_ESTIMATOR_INTERNAL_H */
