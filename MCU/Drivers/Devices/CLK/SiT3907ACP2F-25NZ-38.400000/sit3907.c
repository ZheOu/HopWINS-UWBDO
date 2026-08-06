/**
  ******************************************************************************
  * @file           : sit3907.c
  * @brief          : SiT3907ACP2F-25NZ-38.400000 DCXO driver
  ******************************************************************************
  */

#include "sit3907.h"

#include <stddef.h>

/* Exact-part protocol constants: address 0, Mode 2, 23-bit control word. */
#define SIT3907_HEADER                  UINT16_C(0xFA0A)
#define SIT3907_REG_MAIN                UINT8_C(0x06)
#define SIT3907_REG_FINE                UINT8_C(0x07)
#define SIT3907_FRAME_BITS              UINT32_C(40)
#define SIT3907_CONTROL_WORD_LIMIT      INT32_C(4194303)
#define SIT3907_CONTROL_WORD_MASK       UINT32_C(0x007FFFFF)
#define SIT3907_FINE_MASK               UINT32_C(0x0000007F)
#define SIT3907_FINE_BITS               7U

/* 1.00135625 = 160217 / 160000, retained for integer-only scaling. */
#define SIT3907_SCALE_CORRECTION_NUM    INT64_C(160217)
#define SIT3907_SCALE_CORRECTION_DEN    INT64_C(160000)
#define SIT3907_PPB_PER_PPM             INT64_C(1000)

/* Safe margins over the 500 ns level minimum and 2 us frame-gap minimum. */
#define SIT3907_LOGIC_LEVEL_US          UINT32_C(2)
#define SIT3907_MIDDLE_LEVEL_US         UINT32_C(3)
#define SIT3907_FRAME_GAP_US            UINT32_C(4)
/* Datasheet maximum frequency-change delay plus margin. */
#define SIT3907_FREQUENCY_SETTLE_US     UINT32_C(40)

static bool platform_is_valid(const sit3907_platform_t *platform);
static sit3907_status_t ppb_to_control_word(int32_t ppb, int32_t *control_word);
static void write_control_word(
    const sit3907_device_t *device,
    int32_t control_word);
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

sit3907_status_t sit3907_init(
    sit3907_device_t *device,
    const sit3907_platform_t *platform)
{
  if ((device == NULL) || !platform_is_valid(platform)) {
    return SIT3907_STATUS_BAD_ARG;
  }

  device->initialized = false;
  device->platform = platform;
  device->frequency_control_word = 0;
  device->pull_ppb = 0;

  /* Return-to-middle signaling idles in the externally biased middle state. */
  platform->dp_release();
  platform->delay_us(SIT3907_MIDDLE_LEVEL_US);

  device->initialized = true;
  return SIT3907_STATUS_OK;
}

sit3907_status_t sit3907_set_pull_ppb(
    sit3907_device_t *device,
    int32_t ppb)
{
  sit3907_status_t status;
  int32_t control_word;

  if (device == NULL) {
    return SIT3907_STATUS_BAD_ARG;
  }
  if (!device->initialized) {
    return SIT3907_STATUS_NOT_INITIALIZED;
  }

  status = ppb_to_control_word(ppb, &control_word);
  if (status != SIT3907_STATUS_OK) {
    return status;
  }

  write_control_word(device, control_word);
  device->frequency_control_word = control_word;
  device->pull_ppb = ppb;
  return SIT3907_STATUS_OK;
}

sit3907_status_t sit3907_center(sit3907_device_t *device)
{
  return sit3907_set_pull_ppb(device, 0);
}

static bool platform_is_valid(const sit3907_platform_t *platform)
{
  return (platform != NULL) &&
         (platform->dp_drive_high != NULL) &&
         (platform->dp_drive_low != NULL) &&
         (platform->dp_release != NULL) &&
         (platform->delay_us != NULL);
}

static sit3907_status_t ppb_to_control_word(
    int32_t ppb,
    int32_t *control_word)
{
  int64_t numerator;
  int64_t denominator;
  int64_t result;

  if (control_word == NULL) {
    return SIT3907_STATUS_BAD_ARG;
  }
  if ((ppb > SIT3907_MAX_PULL_PPB) ||
      (ppb < -SIT3907_MAX_PULL_PPB)) {
    return SIT3907_STATUS_OUT_OF_RANGE;
  }

  /*
   * code = round(ppb * full_scale * 160000 /
   *              (1000 * pull_range_ppm * 160217))
   */
  numerator = (int64_t)ppb * SIT3907_CONTROL_WORD_LIMIT *
              (SIT3907_SCALE_CORRECTION_DEN / SIT3907_PPB_PER_PPM);
  denominator = (int64_t)SIT3907_PULL_RANGE_PPM *
                SIT3907_SCALE_CORRECTION_NUM;
  result = divide_rounded(numerator, denominator);

  if ((result > SIT3907_CONTROL_WORD_LIMIT) ||
      (result < -SIT3907_CONTROL_WORD_LIMIT)) {
    return SIT3907_STATUS_OUT_OF_RANGE;
  }

  *control_word = (int32_t)result;
  return SIT3907_STATUS_OK;
}

static void write_control_word(
    const sit3907_device_t *device,
    int32_t control_word)
{
  uint32_t raw = (uint32_t)control_word & SIT3907_CONTROL_WORD_MASK;

  /* The fine register is loaded first; writing the main register applies it. */
  send_frame(
      device,
      SIT3907_REG_FINE,
      (uint16_t)(raw & SIT3907_FINE_MASK));
  device->platform->delay_us(SIT3907_FRAME_GAP_US);
  send_frame(
      device,
      SIT3907_REG_MAIN,
      (uint16_t)(raw >> SIT3907_FINE_BITS));
  device->platform->delay_us(SIT3907_FREQUENCY_SETTLE_US);
}

static int64_t divide_rounded(int64_t numerator, int64_t denominator)
{
  int64_t half = denominator / 2;

  if (numerator >= 0) {
    return (numerator + half) / denominator;
  }
  return -((-numerator + half) / denominator);
}

static void send_frame(
    const sit3907_device_t *device,
    uint8_t reg_address,
    uint16_t value)
{
  uint64_t frame = ((uint64_t)SIT3907_HEADER << 24U) |
                   ((uint64_t)reg_address << 16U) |
                   value;

  send_bits(device, frame, SIT3907_FRAME_BITS);
}

static void send_bits(
    const sit3907_device_t *device,
    uint64_t bits,
    uint32_t bit_count)
{
  for (uint32_t i = bit_count; i > 0U; i--) {
    send_bit(device, ((bits >> (i - 1U)) & UINT64_C(1)) != 0U);
  }
}

static void send_bit(const sit3907_device_t *device, bool one)
{
  if (one) {
    device->platform->dp_drive_high();
  } else {
    device->platform->dp_drive_low();
  }
  device->platform->delay_us(SIT3907_LOGIC_LEVEL_US);

  device->platform->dp_release();
  device->platform->delay_us(SIT3907_MIDDLE_LEVEL_US);
}
