/**
  ******************************************************************************
  * @file           : sit3907.c
  * @brief          : SiTime SiT3907 digitally controlled oscillator (DCXO) driver
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "sit3907.h"

/* Private define ------------------------------------------------------------*/

/* Datasheet: header is 0xFAxA, x being the factory device address nibble. */
#define SIT3907_HEADER_BASE        UINT16_C(0xFA0A)
#define SIT3907_HEADER_ADDR_SHIFT  4U
#define SIT3907_HEADER_ADDR_MAX    15U

/* DCXO registers. 0x06 takes the upper 16 bits, 0x07 the low 7. */
#define SIT3907_REG_MAIN           UINT8_C(0x06)
#define SIT3907_REG_FINE           UINT8_C(0x07)

#define SIT3907_FRAME_BITS         40U

/* Full-scale magnitude per mode: 2^15-1 and 2^22-1. */
#define SIT3907_FULL_SCALE_MODE_1  INT32_C(32767)
#define SIT3907_FULL_SCALE_MODE_2  INT32_C(4194303)

/* Mode 2 sends a 23-bit 2's complement value split 16 + 7. */
#define SIT3907_MODE_2_VALUE_MASK  UINT32_C(0x7FFFFF)
#define SIT3907_MODE_2_FINE_MASK   UINT32_C(0x7F)
#define SIT3907_MODE_2_FINE_BITS   7U

/*
 * The datasheet's 1.00135625 correction, kept as an exact fraction so the whole
 * conversion stays in integers: 1.00135625 = 160217 / 160000.
 */
#define SIT3907_CORRECTION_NUM     INT64_C(160217)
#define SIT3907_CORRECTION_DEN     INT64_C(160000)

/* ppm to ppb. */
#define SIT3907_PPB_PER_PPM        INT64_C(1000)

/* T_logic and T_middle minimums are 500 ns; 1 us is the coarsest safe tick. */
#define SIT3907_MIN_LEVEL_US       1U

/* Frame-to-frame delay, datasheet minimum 2 us. */
#define SIT3907_FRAME_GAP_US       4U

/* Frequency settling time after the updating frame, datasheet maximum 30 us. */
#define SIT3907_SETTLE_US          40U

/* Private function prototypes -----------------------------------------------*/

static bool platform_is_valid(const sit3907_platform_t *platform);
static bool pull_range_is_valid(uint16_t pull_range_ppm);
static int32_t full_scale_for_mode(sit3907_mode_t mode);
static int64_t divide_rounded(int64_t numerator, int64_t denominator);
static void send_frame(
    const sit3907_device_t *device,
    uint8_t reg_address,
    uint16_t value);
static void send_bits(
    const sit3907_device_t *device,
    uint64_t bits,
    uint32_t bit_count);
static void send_bit(const sit3907_device_t *device, bool one);

/* Public functions ----------------------------------------------------------*/

sit3907_status_t sit3907_init(
    sit3907_device_t *device,
    const sit3907_platform_t *platform,
    const sit3907_config_t *config)
{
  int32_t full_scale;

  if ((device == NULL) || (config == NULL) || !platform_is_valid(platform)) {
    return SIT3907_STATUS_BAD_ARG;
  }

  if ((config->device_address > SIT3907_HEADER_ADDR_MAX) ||
      !pull_range_is_valid(config->pull_range_ppm)) {
    return SIT3907_STATUS_BAD_ARG;
  }

  full_scale = full_scale_for_mode(config->mode);
  if (full_scale == 0) {
    return SIT3907_STATUS_BAD_ARG;
  }

  device->config = *config;
  device->platform = platform;

  /* Clamp both level times up to the datasheet minimum rather than rejecting
     a zero, so a caller that leaves them unset still gets a legal waveform. */
  if (device->config.t_logic_us < SIT3907_MIN_LEVEL_US) {
    device->config.t_logic_us = SIT3907_MIN_LEVEL_US;
  }
  if (device->config.t_middle_us < SIT3907_MIN_LEVEL_US) {
    device->config.t_middle_us = SIT3907_MIN_LEVEL_US;
  }

  device->header = (uint16_t)(
      SIT3907_HEADER_BASE |
      ((uint16_t)config->device_address << SIT3907_HEADER_ADDR_SHIFT));
  device->code_limit = full_scale;
  device->last_code = 0;
  device->last_ppb = 0;
  device->writes = 0U;

  /* Return-to-middle signalling idles at mid, so that is the parked state. */
  platform->dp_release();
  platform->delay_us(device->config.t_middle_us);

  device->initialized = true;
  return SIT3907_STATUS_OK;
}

sit3907_status_t sit3907_ppb_to_code(
    const sit3907_device_t *device,
    int32_t ppb,
    int32_t *code)
{
  int64_t numerator;
  int64_t denominator;
  int64_t result;

  if ((device == NULL) || (code == NULL)) {
    return SIT3907_STATUS_BAD_ARG;
  }
  if (!device->initialized) {
    return SIT3907_STATUS_NOT_INITIALIZED;
  }

  /*
   * code = round(ppm * full_scale / (pull_range_ppm * 1.00135625))
   *
   * with ppm = ppb / 1000 and the correction expanded to 160217 / 160000:
   *
   *   code = round(ppb * full_scale * 160000
   *                / (1000 * pull_range_ppm * 160217))
   *        = round(ppb * full_scale * 160 / (pull_range_ppm * 160217))
   *
   * Worst case magnitude is 1.6e6 ppb * 4194303 * 160 which is about 1.1e15,
   * comfortably inside int64.
   */
  numerator = (int64_t)ppb * (int64_t)device->code_limit *
              (SIT3907_CORRECTION_DEN / SIT3907_PPB_PER_PPM);
  denominator = (int64_t)device->config.pull_range_ppm * SIT3907_CORRECTION_NUM;

  result = divide_rounded(numerator, denominator);

  if ((result > (int64_t)device->code_limit) ||
      (result < -(int64_t)device->code_limit)) {
    return SIT3907_STATUS_OUT_OF_RANGE;
  }

  *code = (int32_t)result;
  return SIT3907_STATUS_OK;
}

sit3907_status_t sit3907_code_to_ppb(
    const sit3907_device_t *device,
    int32_t code,
    int32_t *ppb)
{
  int64_t numerator;
  int64_t denominator;

  if ((device == NULL) || (ppb == NULL)) {
    return SIT3907_STATUS_BAD_ARG;
  }
  if (!device->initialized) {
    return SIT3907_STATUS_NOT_INITIALIZED;
  }
  if ((code > device->code_limit) || (code < -device->code_limit)) {
    return SIT3907_STATUS_OUT_OF_RANGE;
  }

  numerator = (int64_t)code * (int64_t)device->config.pull_range_ppm *
              SIT3907_CORRECTION_NUM;
  denominator = (int64_t)device->code_limit *
                (SIT3907_CORRECTION_DEN / SIT3907_PPB_PER_PPM);

  *ppb = (int32_t)divide_rounded(numerator, denominator);
  return SIT3907_STATUS_OK;
}

sit3907_status_t sit3907_step_ppb(const sit3907_device_t *device, int32_t *ppb)
{
  sit3907_status_t status = sit3907_code_to_ppb(device, 1, ppb);

  /*
   * One code is finer than 1 ppb in mode 2 at every pull range, so the integer
   * conversion lands on zero there. Report 1 instead: the datasheet's own
   * resolution table does the same, quoting 1 ppb across all mode 2 ranges, and
   * a caller sizing a control-loop step needs the smallest pull it can actually
   * command rather than a zero it has to special-case.
   *
   * Mode 1 needs no such floor and reproduces the table exactly, 49 ppb at
   * +/-1600 ppm down to 1 ppb at +/-25 ppm.
   */
  if ((status == SIT3907_STATUS_OK) && (*ppb < 1)) {
    *ppb = 1;
  }
  return status;
}

sit3907_status_t sit3907_set_pull_code(sit3907_device_t *device, int32_t code)
{
  uint32_t raw;

  if (device == NULL) {
    return SIT3907_STATUS_BAD_ARG;
  }
  if (!device->initialized) {
    return SIT3907_STATUS_NOT_INITIALIZED;
  }
  if ((code > device->code_limit) || (code < -device->code_limit)) {
    return SIT3907_STATUS_OUT_OF_RANGE;
  }

  if (device->config.mode == SIT3907_MODE_1) {
    /* Single frame; the frequency moves at the end of it. */
    send_frame(device, SIT3907_REG_MAIN, (uint16_t)(int16_t)code);
  } else {
    /*
     * Two frames. 0x07 holds the low 7 bits and must go first, which arms the
     * update without applying it; 0x06 holds the upper 16 and applies it.
     * Masking to 23 bits is the 2's complement conversion: for negative code
     * this is code modulo 2^23, exactly what the datasheet examples show.
     */
    raw = (uint32_t)code & SIT3907_MODE_2_VALUE_MASK;

    send_frame(device, SIT3907_REG_FINE, (uint16_t)(raw & SIT3907_MODE_2_FINE_MASK));
    device->platform->delay_us(SIT3907_FRAME_GAP_US);
    send_frame(
        device,
        SIT3907_REG_MAIN,
        (uint16_t)((raw >> SIT3907_MODE_2_FINE_BITS) & UINT32_C(0xFFFF)));
  }

  /* Let the output settle before the caller measures or writes again. */
  device->platform->delay_us(SIT3907_SETTLE_US);

  device->last_code = code;
  (void)sit3907_code_to_ppb(device, code, &device->last_ppb);
  device->writes++;
  return SIT3907_STATUS_OK;
}

sit3907_status_t sit3907_set_pull_ppb(sit3907_device_t *device, int32_t ppb)
{
  sit3907_status_t status;
  int32_t code = 0;

  status = sit3907_ppb_to_code(device, ppb, &code);
  if (status != SIT3907_STATUS_OK) {
    return status;
  }

  status = sit3907_set_pull_code(device, code);
  if (status == SIT3907_STATUS_OK) {
    /* Report what was asked for; last_code holds what was actually applied. */
    device->last_ppb = ppb;
  }
  return status;
}

sit3907_status_t sit3907_center(sit3907_device_t *device)
{
  return sit3907_set_pull_code(device, 0);
}

/* Private functions ---------------------------------------------------------*/

static bool platform_is_valid(const sit3907_platform_t *platform)
{
  return (platform != NULL) &&
         (platform->dp_drive_high != NULL) &&
         (platform->dp_drive_low != NULL) &&
         (platform->dp_release != NULL) &&
         (platform->delay_us != NULL);
}

static bool pull_range_is_valid(uint16_t pull_range_ppm)
{
  switch (pull_range_ppm) {
    case 25U:
    case 50U:
    case 100U:
    case 200U:
    case 400U:
    case 800U:
    case 1600U:
      return true;
    default:
      return false;
  }
}

static int32_t full_scale_for_mode(sit3907_mode_t mode)
{
  switch (mode) {
    case SIT3907_MODE_1:
      return SIT3907_FULL_SCALE_MODE_1;
    case SIT3907_MODE_2:
      return SIT3907_FULL_SCALE_MODE_2;
    default:
      return 0;
  }
}

/* Rounds half away from zero, symmetric so a pull and its negative agree. */
static int64_t divide_rounded(int64_t numerator, int64_t denominator)
{
  int64_t half = denominator / 2;

  if (numerator >= 0) {
    return (numerator + half) / denominator;
  }
  return -((-numerator + half) / denominator);
}

/*
 * One 40-bit frame, most significant bit first: header, register, value.
 */
static void send_frame(
    const sit3907_device_t *device,
    uint8_t reg_address,
    uint16_t value)
{
  uint64_t frame = ((uint64_t)device->header << 24) |
                   ((uint64_t)reg_address << 16) |
                   (uint64_t)value;

  send_bits(device, frame, SIT3907_FRAME_BITS);
}

static void send_bits(
    const sit3907_device_t *device,
    uint64_t bits,
    uint32_t bit_count)
{
  for (uint32_t i = bit_count; i > 0U; i--) {
    send_bit(device, ((bits >> (i - 1U)) & UINT64_C(1)) != UINT64_C(0));
  }
}

/*
 * Return-to-middle: the level carries the bit value, and the mid period that
 * follows is what makes the bit self-clocking. Both halves are required.
 */
static void send_bit(const sit3907_device_t *device, bool one)
{
  if (one) {
    device->platform->dp_drive_high();
  } else {
    device->platform->dp_drive_low();
  }
  device->platform->delay_us(device->config.t_logic_us);

  device->platform->dp_release();
  device->platform->delay_us(device->config.t_middle_us);
}
