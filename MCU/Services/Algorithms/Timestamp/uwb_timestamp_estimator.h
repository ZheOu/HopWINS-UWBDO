/**
  ******************************************************************************
  * @file           : uwb_timestamp_estimator.h
  * @brief          : Selectable UWB receive timestamp estimators
  ******************************************************************************
  */

#ifndef HOPWINS_UWB_TIMESTAMP_ESTIMATOR_H
#define HOPWINS_UWB_TIMESTAMP_ESTIMATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "uwb_service.h"

typedef enum {
  UWB_TIMESTAMP_ESTIMATOR_DW_ADJUSTED = 0,
  UWB_TIMESTAMP_ESTIMATOR_DW_RAW,
} uwb_timestamp_estimator_id_t;

typedef enum {
  UWB_TIMESTAMP_RESULT_OK = 0,
  UWB_TIMESTAMP_RESULT_PENDING,
  UWB_TIMESTAMP_RESULT_INVALID_CAPTURE,
} uwb_timestamp_result_status_t;

typedef struct {
  uwb_timestamp_result_status_t status;
  uwb_timestamp_estimator_id_t estimator;
  uint64_t timestamp_dtu;
  uint32_t capture_id;
  uint16_t quality_q15;
} uwb_timestamp_result_t;

typedef struct {
  uwb_timestamp_estimator_id_t estimator;
  const char *name;
  uint32_t accepted_count;
  uint32_t pending_count;
  uint32_t rejected_count;
  bool requires_cir;
  bool initialized;
} uwb_timestamp_estimator_state_t;

bool uwb_timestamp_estimator_init(uwb_timestamp_estimator_id_t estimator);
uwb_timestamp_result_status_t uwb_timestamp_estimator_process(
    const uwb_service_cir_capture_t *capture,
    uwb_timestamp_result_t *result);
const uwb_timestamp_estimator_state_t *
uwb_timestamp_estimator_get_state(void);
const char *uwb_timestamp_estimator_name(
    uwb_timestamp_estimator_id_t estimator);
bool uwb_timestamp_estimator_requires_cir(
    uwb_timestamp_estimator_id_t estimator);
bool uwb_timestamp_estimator_is_supported(
    uwb_timestamp_estimator_id_t estimator);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_UWB_TIMESTAMP_ESTIMATOR_H */
