/**
  ******************************************************************************
  * @file           : uwb_timestamp_estimator.c
  * @brief          : Runtime selection of a UWB timestamp estimator
  ******************************************************************************
  */

#include "uwb_timestamp_estimator.h"

#include "uwb_timestamp_estimator_internal.h"

#include <stddef.h>
#include <string.h>

static const uwb_timestamp_estimator_strategy_t *const s_strategies[] = {
  &g_uwb_timestamp_dw_adjusted_strategy,
  &g_uwb_timestamp_dw_raw_strategy,
};

static const uwb_timestamp_estimator_strategy_t *s_strategy;
static uwb_timestamp_estimator_state_t s_state;

static const uwb_timestamp_estimator_strategy_t *find_strategy(
    uwb_timestamp_estimator_id_t estimator);

bool uwb_timestamp_estimator_init(uwb_timestamp_estimator_id_t estimator)
{
  s_strategy = find_strategy(estimator);
  memset(&s_state, 0, sizeof(s_state));
  s_state.estimator = estimator;
  if (s_strategy == NULL) {
    return false;
  }

  if (s_strategy->reset != NULL) {
    s_strategy->reset();
  }
  s_state.name = s_strategy->name;
  s_state.requires_cir = s_strategy->requires_cir;
  s_state.initialized = true;
  return true;
}

uwb_timestamp_result_status_t uwb_timestamp_estimator_process(
    const uwb_service_cir_capture_t *capture,
    uwb_timestamp_result_t *result)
{
  uwb_timestamp_result_status_t status;

  if (result != NULL) {
    *result = (uwb_timestamp_result_t){
      .status = UWB_TIMESTAMP_RESULT_INVALID_CAPTURE,
      .estimator = s_state.estimator,
    };
  }
  if (!s_state.initialized || (s_strategy == NULL) ||
      (result == NULL)) {
    return UWB_TIMESTAMP_RESULT_INVALID_CAPTURE;
  }

  status = s_strategy->process(capture, result);
  result->status = status;
  result->estimator = s_state.estimator;
  if (status == UWB_TIMESTAMP_RESULT_OK) {
    s_state.accepted_count++;
  } else if (status == UWB_TIMESTAMP_RESULT_PENDING) {
    s_state.pending_count++;
  } else {
    s_state.rejected_count++;
  }
  return status;
}

const uwb_timestamp_estimator_state_t *
uwb_timestamp_estimator_get_state(void)
{
  return &s_state;
}

const char *uwb_timestamp_estimator_name(
    uwb_timestamp_estimator_id_t estimator)
{
  const uwb_timestamp_estimator_strategy_t *strategy =
      find_strategy(estimator);

  return (strategy != NULL) ? strategy->name : "INVALID";
}

bool uwb_timestamp_estimator_requires_cir(
    uwb_timestamp_estimator_id_t estimator)
{
  const uwb_timestamp_estimator_strategy_t *strategy =
      find_strategy(estimator);

  return (strategy != NULL) && strategy->requires_cir;
}

bool uwb_timestamp_estimator_is_supported(
    uwb_timestamp_estimator_id_t estimator)
{
  return find_strategy(estimator) != NULL;
}

static const uwb_timestamp_estimator_strategy_t *find_strategy(
    uwb_timestamp_estimator_id_t estimator)
{
  for (size_t i = 0U; i < (sizeof(s_strategies) / sizeof(s_strategies[0]));
       i++) {
    if (s_strategies[i]->id == estimator) {
      return s_strategies[i];
    }
  }
  return NULL;
}
