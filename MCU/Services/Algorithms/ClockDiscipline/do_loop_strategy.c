/**
  ******************************************************************************
  * @file           : do_loop_strategy.c
  * @brief          : Pure frequency-error strategies for the DO control loop
  ******************************************************************************
  */

#include "do_loop_strategy.h"

#include <stddef.h>

#define DO_WEIGHTED_BASELINE_MIN_INTERVALS UINT32_C(4)

bool do_loop_strategy_init(
    do_loop_strategy_t *strategy,
    do_loop_strategy_id_t id)
{
  if ((strategy == NULL) ||
      ((id != DO_LOOP_STRATEGY_ENDPOINT_SLOPE) &&
       (id != DO_LOOP_STRATEGY_WEIGHTED_BASELINES))) {
    return false;
  }

  *strategy = (do_loop_strategy_t){.id = id};
  return true;
}

void do_loop_strategy_reset(do_loop_strategy_t *strategy)
{
  if (strategy == NULL) {
    return;
  }

  strategy->weighted_error_sum_ppb = 0;
  strategy->weight_sum = 0U;
  strategy->observation_count = 0U;
}

bool do_loop_strategy_observe(
    do_loop_strategy_t *strategy,
    uint32_t interval_count,
    int32_t endpoint_error_ppb,
    bool window_complete,
    int32_t *estimated_error_ppb)
{
  int64_t rounded_sum;
  uint64_t weight;

  if ((strategy == NULL) || (estimated_error_ppb == NULL)) {
    return false;
  }

  if (strategy->id == DO_LOOP_STRATEGY_ENDPOINT_SLOPE) {
    if (!window_complete) {
      return false;
    }
    strategy->observation_count = 1U;
    *estimated_error_ppb = endpoint_error_ppb;
    return true;
  }

  if (strategy->id != DO_LOOP_STRATEGY_WEIGHTED_BASELINES) {
    return false;
  }

  if (interval_count >= DO_WEIGHTED_BASELINE_MIN_INTERVALS) {
    weight = (uint64_t)interval_count * interval_count;
    strategy->weighted_error_sum_ppb +=
        (int64_t)endpoint_error_ppb * (int64_t)weight;
    strategy->weight_sum += weight;
    strategy->observation_count++;
  }
  if (!window_complete || (strategy->weight_sum == 0U)) {
    return false;
  }

  rounded_sum = strategy->weighted_error_sum_ppb;
  if (rounded_sum >= 0) {
    rounded_sum += (int64_t)(strategy->weight_sum / 2U);
  } else {
    rounded_sum -= (int64_t)(strategy->weight_sum / 2U);
  }
  *estimated_error_ppb =
      (int32_t)(rounded_sum / (int64_t)strategy->weight_sum);
  return true;
}

const char *do_loop_strategy_name(do_loop_strategy_id_t id)
{
  switch (id) {
    case DO_LOOP_STRATEGY_ENDPOINT_SLOPE:
      return "ENDPOINT_SLOPE";
    case DO_LOOP_STRATEGY_WEIGHTED_BASELINES:
      return "WEIGHTED_BASELINES";
    default:
      return "INVALID";
  }
}

bool do_loop_strategy_is_supported(do_loop_strategy_id_t id)
{
  return (id == DO_LOOP_STRATEGY_ENDPOINT_SLOPE) ||
      (id == DO_LOOP_STRATEGY_WEIGHTED_BASELINES);
}
