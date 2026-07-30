/**
  ******************************************************************************
  * @file           : clock_service.h
  * @brief          : SiT3907 DCXO service: pull control and a bring-up sweep
  *
  * Owns the SiT3907 device state and the platform glue that maps the driver's
  * three DP levels onto board_clkdp_set_mode(). Per Services/README.md the
  * service returns structured state and never prints; the console layer decides
  * how to present it.
  ******************************************************************************
  */

#ifndef HOPWINS_CLOCK_SERVICE_H
#define HOPWINS_CLOCK_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "board.h"
#include "sit3907.h"

/** @brief Number of steps recorded by the bring-up sweep. */
#define CLOCK_SERVICE_SWEEP_STEPS 5U

typedef struct {
  /** Pull that was requested, in ppb. */
  int32_t requested_ppb;
  /** Code the driver actually wrote. */
  int32_t code;
  /** Pull that code represents, so quantisation error is visible. */
  int32_t applied_ppb;
  /** External clock counter delta measured over the dwell, TIM2 ETR ticks. */
  uint32_t counter_delta;
  /**
    * Counts this pull should produce over the dwell, from the nominal frequency.
    * Comparing it against counter_delta is what makes the sweep self-checking.
    */
  uint32_t expected_delta;
  /** Milliseconds the counter was allowed to run. */
  uint32_t dwell_ms;
  sit3907_status_t status;
} clock_service_sweep_point_t;

typedef struct {
  bool initialized;
  sit3907_status_t init_status;
  /** Smallest expressible step for the configured mode and pull range. */
  int32_t step_ppb;
  /** Widest pull the configured part accepts, in ppb. */
  int32_t range_ppb;
  uint32_t writes;
  int32_t last_requested_ppb;
  int32_t last_code;
  /** Populated by clock_service_run_pull_sweep(). */
  bool sweep_valid;
  uint32_t sweep_count;
  clock_service_sweep_point_t sweep[CLOCK_SERVICE_SWEEP_STEPS];
} clock_service_state_t;

/**
  * @brief Select the CLKDP oscillator, park DP at mid, and validate the part.
  *
  * Does not move the frequency: the part powers up at its factory frequency with
  * both DCXO registers zeroed, and this leaves it there.
  *
  * The pull range is not a parameter. It is fixed by the part fitted on this
  * board and taken from SIT3907_PULL_RANGE_PPM, because passing it in only
  * creates a value that can be filled in wrong without any error appearing.
  *
  * @param device_address Factory-programmed header nibble, 0 unless the part was
  *                       ordered otherwise.
  */
sit3907_status_t clock_service_init(
    sit3907_mode_t mode,
    uint8_t device_address);

/** @brief Apply a pull, in parts per billion. Positive raises the frequency. */
sit3907_status_t clock_service_set_pull_ppb(int32_t ppb);

/** @brief Return the oscillator to the centre of its pull range. */
sit3907_status_t clock_service_center(void);

/**
  * @brief Walk a set of pulls and count the external clock at each one.
  *
  * The bring-up test. It answers one question, "does the DP wire work", by
  * showing the TIM2 ETR count move with the commanded pull. Blocks for roughly
  * CLOCK_SERVICE_SWEEP_STEPS * @p dwell_ms.
  *
  * The pulls it uses are deliberately large, hundreds of ppm, because the gate
  * for the counter is the MCU SysTick and therefore MSI, an independent internal
  * oscillator. That measures the XO against MSI, so MSI's own instability sets
  * the noise floor: over a one second gate it is on the order of a ppm, which is
  * the same size as a 1 ppm pull. Small pulls belong in the control loop once
  * there is a real reference; a wiring test needs a signal that cannot be
  * confused with drift.
  *
  * @param dwell_ms Counter gate per step. At 38.4 MHz one ppm is 38.4 counts per
  *                 second, so 1000 ms is a reasonable floor.
  */
sit3907_status_t clock_service_run_pull_sweep(uint32_t dwell_ms);

const clock_service_state_t *clock_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_CLOCK_SERVICE_H */
