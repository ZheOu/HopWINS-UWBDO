/**
  ******************************************************************************
  * @file           : do_loop_strategy.h
  * @brief          : Pure frequency-error strategies for the DO control loop
  ******************************************************************************
  */

#ifndef HOPWINS_DO_LOOP_STRATEGY_H
#define HOPWINS_DO_LOOP_STRATEGY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  DO_LOOP_STRATEGY_ENDPOINT_SLOPE = 0,
  DO_LOOP_STRATEGY_WEIGHTED_BASELINES,
} do_loop_strategy_id_t;

typedef struct {
  do_loop_strategy_id_t id;
  int64_t weighted_error_sum_ppb;
  uint64_t weight_sum;
  uint32_t observation_count;
} do_loop_strategy_t;

bool do_loop_strategy_init(
    do_loop_strategy_t *strategy,
    do_loop_strategy_id_t id);
void do_loop_strategy_reset(do_loop_strategy_t *strategy);
bool do_loop_strategy_observe(
    do_loop_strategy_t *strategy,
    uint32_t interval_count,
    int32_t endpoint_error_ppb,
    bool window_complete,
    int32_t *estimated_error_ppb);
const char *do_loop_strategy_name(do_loop_strategy_id_t id);
bool do_loop_strategy_is_supported(do_loop_strategy_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_DO_LOOP_STRATEGY_H */
