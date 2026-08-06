/**
  ******************************************************************************
  * @file           : uwb_timestamp_dw.c
  * @brief          : Native DW3000 receive timestamp strategies
  ******************************************************************************
  */

#include "uwb_timestamp_estimator_internal.h"

#include <stddef.h>

#define DW_TIMESTAMP_MASK UINT64_C(0xFFFFFFFFFF)

static uwb_timestamp_result_status_t process_adjusted(
    const uwb_service_cir_capture_t *capture,
    uwb_timestamp_result_t *result);
static uwb_timestamp_result_status_t process_raw(
    const uwb_service_cir_capture_t *capture,
    uwb_timestamp_result_t *result);

const uwb_timestamp_estimator_strategy_t
    g_uwb_timestamp_dw_adjusted_strategy = {
  .id = UWB_TIMESTAMP_ESTIMATOR_DW_ADJUSTED,
  .name = "DW_ADJUSTED",
  .requires_cir = false,
  .reset = NULL,
  .process = process_adjusted,
};

const uwb_timestamp_estimator_strategy_t
    g_uwb_timestamp_dw_raw_strategy = {
  .id = UWB_TIMESTAMP_ESTIMATOR_DW_RAW,
  .name = "DW_RAW",
  .requires_cir = false,
  .reset = NULL,
  .process = process_raw,
};

static uwb_timestamp_result_status_t process_adjusted(
    const uwb_service_cir_capture_t *capture,
    uwb_timestamp_result_t *result)
{
  if ((capture == NULL) || (result == NULL)) {
    return UWB_TIMESTAMP_RESULT_INVALID_CAPTURE;
  }

  result->timestamp_dtu = capture->receive_timestamp & DW_TIMESTAMP_MASK;
  result->capture_id = capture->capture_id;
  result->quality_q15 = UINT16_MAX;
  return UWB_TIMESTAMP_RESULT_OK;
}

static uwb_timestamp_result_status_t process_raw(
    const uwb_service_cir_capture_t *capture,
    uwb_timestamp_result_t *result)
{
  if ((capture == NULL) || (result == NULL)) {
    return UWB_TIMESTAMP_RESULT_INVALID_CAPTURE;
  }

  result->timestamp_dtu =
      capture->raw_receive_timestamp & DW_TIMESTAMP_MASK;
  result->capture_id = capture->capture_id;
  result->quality_q15 = UINT16_MAX;
  return UWB_TIMESTAMP_RESULT_OK;
}
