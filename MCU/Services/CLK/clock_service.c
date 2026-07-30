/**
  ******************************************************************************
  * @file           : clock_service.c
  * @brief          : SiT3907 DCXO service: pull control and a bring-up sweep
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "clock_service.h"

/* Private define ------------------------------------------------------------*/

/*
 * DP level and mid times. board_clkdp_set_mode() reconfigures the pin through
 * HAL_GPIO_Init(), which already costs a good fraction of a microsecond, so
 * these are on top of that. The datasheet minimum is 500 ns for both with no
 * maximum, and the mid level is an RC settle through the external divider, so
 * erring long here is free.
 */
#define CLOCK_SERVICE_T_LOGIC_US   2U
#define CLOCK_SERVICE_T_MIDDLE_US  3U

/* Private variables ---------------------------------------------------------*/

static sit3907_device_t s_dcxo;
static clock_service_state_t s_state;

/* Private function prototypes -----------------------------------------------*/

static void dp_drive_high(void);
static void dp_drive_low(void);
static void dp_release(void);
static void dp_delay_us(uint32_t delay_us);
static uint32_t measure_external_clock(uint32_t dwell_ms);
static uint32_t expected_counts(int32_t ppb, uint32_t dwell_ms);

static const sit3907_platform_t s_dcxo_platform = {
  .dp_drive_high = dp_drive_high,
  .dp_drive_low = dp_drive_low,
  .dp_release = dp_release,
  .delay_us = dp_delay_us,
};

/* Public functions ----------------------------------------------------------*/

sit3907_status_t clock_service_init(
    sit3907_mode_t mode,
    uint8_t device_address)
{
  sit3907_config_t config;
  sit3907_status_t status;

  s_state = (clock_service_state_t){0};

  /*
   * The board supports two oscillator variants on mutually exclusive
   * interfaces. Selecting CLKDP is what hands the DP pin to this service; it
   * also parks the pin tri-stated, which is the mid level the protocol idles at.
   */
  board_clock_select_xo(BOARD_CLOCK_XO_CLKDP);

  config.device_address = device_address;
  config.pull_range_ppm = SIT3907_PULL_RANGE_PPM;
  config.mode = mode;
  config.t_logic_us = CLOCK_SERVICE_T_LOGIC_US;
  config.t_middle_us = CLOCK_SERVICE_T_MIDDLE_US;

  status = sit3907_init(&s_dcxo, &s_dcxo_platform, &config);
  s_state.init_status = status;
  if (status != SIT3907_STATUS_OK) {
    return status;
  }

  (void)sit3907_step_ppb(&s_dcxo, &s_state.step_ppb);
  (void)sit3907_code_to_ppb(&s_dcxo, s_dcxo.code_limit, &s_state.range_ppb);

  s_state.initialized = true;
  return SIT3907_STATUS_OK;
}

sit3907_status_t clock_service_set_pull_ppb(int32_t ppb)
{
  sit3907_status_t status;

  if (!s_state.initialized) {
    return SIT3907_STATUS_NOT_INITIALIZED;
  }

  status = sit3907_set_pull_ppb(&s_dcxo, ppb);
  if (status == SIT3907_STATUS_OK) {
    s_state.last_requested_ppb = ppb;
    s_state.last_code = s_dcxo.last_code;
    s_state.writes = s_dcxo.writes;
  }
  return status;
}

sit3907_status_t clock_service_center(void)
{
  return clock_service_set_pull_ppb(0);
}

sit3907_status_t clock_service_run_pull_sweep(uint32_t dwell_ms)
{
  /*
   * Centre, up, down, a much larger step, then back to centre.
   *
   * Out of order and asymmetric on purpose: a DP line that is stuck, inverted,
   * or simply not connected still yields a plausible monotonic table if the
   * sweep only ever ramps one way. Going up, then down past centre, then far up
   * again means a broken wire shows up as a table that does not move at all.
   *
   * Magnitudes are hundreds of ppm rather than single ppm because the counter
   * gate comes from MSI, so a 1 ppm pull is the same size as the measurement
   * noise. At 38.4 MHz over a 1000 ms gate these produce roughly 3840, -3840 and
   * 38400 counts of difference, none of which drift can imitate.
   */
  static const int32_t sweep_ppb[CLOCK_SERVICE_SWEEP_STEPS] = {
    0, 100000, -100000, 1000000, 0,
  };
  sit3907_status_t first_error = SIT3907_STATUS_OK;

  if (!s_state.initialized) {
    return SIT3907_STATUS_NOT_INITIALIZED;
  }

  s_state.sweep_valid = false;
  s_state.sweep_count = 0U;

  for (uint32_t i = 0U; i < CLOCK_SERVICE_SWEEP_STEPS; i++) {
    clock_service_sweep_point_t *point = &s_state.sweep[i];

    point->requested_ppb = sweep_ppb[i];
    point->dwell_ms = dwell_ms;
    point->code = 0;
    point->applied_ppb = 0;
    point->counter_delta = 0U;
    point->expected_delta = expected_counts(sweep_ppb[i], dwell_ms);

    point->status = sit3907_ppb_to_code(&s_dcxo, sweep_ppb[i], &point->code);
    if (point->status == SIT3907_STATUS_OK) {
      point->status = sit3907_set_pull_code(&s_dcxo, point->code);
    }

    if (point->status == SIT3907_STATUS_OK) {
      (void)sit3907_code_to_ppb(&s_dcxo, point->code, &point->applied_ppb);
      point->counter_delta = measure_external_clock(dwell_ms);
      s_state.last_requested_ppb = sweep_ppb[i];
      s_state.last_code = point->code;
      s_state.writes = s_dcxo.writes;
    } else if (first_error == SIT3907_STATUS_OK) {
      /*
       * A pull outside the configured range is expected to be rejected, and it
       * is recorded rather than aborting: the remaining steps still carry
       * information about whether the wire works at all.
       */
      first_error = point->status;
    }

    s_state.sweep_count = i + 1U;
  }

  s_state.sweep_valid = true;

  /* Never leave the oscillator parked at a sweep value. */
  (void)sit3907_center(&s_dcxo);
  s_state.last_requested_ppb = 0;
  s_state.last_code = 0;
  s_state.writes = s_dcxo.writes;

  return first_error;
}

const clock_service_state_t *clock_service_get_state(void)
{
  return &s_state;
}

/* Private functions ---------------------------------------------------------*/

static void dp_drive_high(void)
{
  board_clkdp_set_mode(BOARD_CLKDP_DRIVE_HIGH);
}

static void dp_drive_low(void)
{
  board_clkdp_set_mode(BOARD_CLKDP_DRIVE_LOW);
}

static void dp_release(void)
{
  /* Tri-state, so the external divider pulls the pin to VIM. */
  board_clkdp_set_mode(BOARD_CLKDP_TRISTATE);
}

/*
 * Microsecond busy-wait. The board layer only exposes a millisecond clock, and
 * the protocol needs sub-microsecond granularity, so this spins instead.
 *
 * The loop count assumes two core cycles per iteration, which is a lower bound
 * for Cortex-M33; a real iteration takes more, so the delay comes out longer
 * than requested. That is the safe direction: T_logic and T_middle are
 * minimums with no maximum, and overshooting only lowers the baud rate.
 */
static void dp_delay_us(uint32_t delay_us)
{
  uint32_t iterations = delay_us * (SystemCoreClock / 2000000U);

  while (iterations != 0U) {
    __NOP();
    iterations--;
  }
}

/*
 * Counts the nominal 38.4 MHz clock would produce over dwell_ms, pulled by ppb.
 *
 *   counts = f_nominal * (1 + ppb/1e9) * dwell_ms / 1000
 *
 * Worst case numerator is 38.4e6 * 1e9 * 60000 scaled down stepwise to stay
 * inside int64; the terms are grouped so nothing exceeds about 2.3e18.
 */
static uint32_t expected_counts(int32_t ppb, uint32_t dwell_ms)
{
  int64_t base = ((int64_t)SIT3907_NOMINAL_HZ * (int64_t)dwell_ms) / 1000;
  int64_t pull = (base * (int64_t)ppb) / INT64_C(1000000000);
  int64_t total = base + pull;

  return (total > 0) ? (uint32_t)total : 0U;
}

/*
 * Counts TIM2 ETR edges for dwell_ms. The external clock arrives on PA0 from
 * the board clock buffer, so this is the one place the firmware can observe the
 * oscillator actually moving.
 */
static uint32_t measure_external_clock(uint32_t dwell_ms)
{
  uint32_t start_ms;
  uint32_t before;
  uint32_t after;

  board_external_clock_counter_reset();
  if (board_external_clock_counter_start() != BOARD_OK) {
    return 0U;
  }

  before = board_external_clock_counter_get();
  start_ms = board_get_time_ms();
  while ((uint32_t)(board_get_time_ms() - start_ms) < dwell_ms) {
    /* Busy-wait: the gate has to be a wall-clock interval, not a loop count. */
  }
  after = board_external_clock_counter_get();

  (void)board_external_clock_counter_stop();
  return after - before;
}
