/**
  ******************************************************************************
  * @file           : sit5156.c
  * @brief          : SiT5156AC-FA025JH-38.400000 DCTCXO driver
  ******************************************************************************
  */

#include "sit5156.h"

#define SIT5156_REG_FREQUENCY_LSW       UINT8_C(0x00)
#define SIT5156_REG_FREQUENCY_MSW_OE    UINT8_C(0x01)
#define SIT5156_REG_PULL_RANGE          UINT8_C(0x02)

#define SIT5156_OE_MASK                 UINT16_C(0x0400)
#define SIT5156_FREQUENCY_MSW_MASK      UINT16_C(0x03FF)
#define SIT5156_FREQUENCY_REGISTER_MASK UINT16_C(0x07FF)
#define SIT5156_MSW_RESERVED_MASK       UINT16_C(0xF800)
#define SIT5156_PULL_RANGE_MASK         UINT16_C(0x000F)
#define SIT5156_PULL_RANGE_CODE         UINT16_C(0x0009)
#define SIT5156_PULL_RESERVED_MASK      UINT16_C(0xFFF0)

#define SIT5156_CONTROL_WORD_MASK       UINT32_C(0x03FFFFFF)
#define SIT5156_CONTROL_WORD_SIGN_BIT   UINT32_C(0x02000000)
#define SIT5156_CONTROL_WORD_LIMIT      INT32_C(33554431)

/* Datasheet maximum frequency-change delay plus settling time: 140 + 20 us. */
#define SIT5156_FREQUENCY_SETTLE_US     UINT32_C(160)
/* Output-enable time is at most 680 ns. */
#define SIT5156_OUTPUT_ENABLE_US        UINT32_C(2)

static bool platform_is_valid(const sit5156_platform_t *platform);
static sit5156_status_t read_snapshot(
    const sit5156_device_t *device,
    sit5156_snapshot_t *snapshot);
static sit5156_status_t read_register(
    const sit5156_device_t *device,
    uint8_t reg_address,
    uint16_t *value);
static sit5156_status_t write_register(
    const sit5156_device_t *device,
    uint8_t reg_address,
    uint16_t value);
static sit5156_status_t ppb_to_control_word(
    int32_t ppb,
    int32_t *control_word);
static sit5156_status_t write_control_word(
    sit5156_device_t *device,
    int32_t control_word);
static void decode_frequency(sit5156_snapshot_t *snapshot);
static int32_t decode_control_word(uint16_t lsw, uint16_t msw_oe);
static int32_t control_word_to_ppb(int32_t control_word);
static int64_t divide_rounded(int64_t numerator, int64_t denominator);

sit5156_status_t sit5156_init(
    sit5156_device_t *device,
    const sit5156_platform_t *platform)
{
  sit5156_snapshot_t snapshot = {0};
  sit5156_status_t status;

  if ((device == NULL) || !platform_is_valid(platform)) {
    return SIT5156_STATUS_BAD_ARG;
  }

  device->initialized = false;
  device->platform = platform;
  device->snapshot = (sit5156_snapshot_t){0};

  status = read_snapshot(device, &snapshot);
  if ((status == SIT5156_STATUS_OK) ||
      (status == SIT5156_STATUS_UNEXPECTED_CONFIG)) {
    /* Preserve complete readback even when it identifies the wrong option. */
    device->snapshot = snapshot;
  }
  if (status != SIT5156_STATUS_OK) {
    return status;
  }

  device->initialized = true;
  return SIT5156_STATUS_OK;
}

sit5156_status_t sit5156_set_output_enabled(
    sit5156_device_t *device,
    bool enabled)
{
  sit5156_status_t status;
  uint16_t expected;
  uint16_t readback;

  if (device == NULL) {
    return SIT5156_STATUS_BAD_ARG;
  }
  if (!device->initialized) {
    return SIT5156_STATUS_NOT_INITIALIZED;
  }

  expected = device->snapshot.frequency_msw_oe &
             SIT5156_FREQUENCY_MSW_MASK;
  if (enabled) {
    expected |= SIT5156_OE_MASK;
  }

  status = write_register(device, SIT5156_REG_FREQUENCY_MSW_OE, expected);
  if (status != SIT5156_STATUS_OK) {
    return status;
  }
  device->platform->delay_us(SIT5156_OUTPUT_ENABLE_US);

  status = read_register(
      device,
      SIT5156_REG_FREQUENCY_MSW_OE,
      &readback);
  if (status != SIT5156_STATUS_OK) {
    return status;
  }

  device->snapshot.frequency_msw_oe = readback;
  decode_frequency(&device->snapshot);
  return ((readback & SIT5156_FREQUENCY_REGISTER_MASK) == expected)
             ? SIT5156_STATUS_OK
             : SIT5156_STATUS_VERIFY_FAILED;
}

sit5156_status_t sit5156_set_pull_ppb(
    sit5156_device_t *device,
    int32_t ppb)
{
  sit5156_status_t status;
  int32_t control_word;

  if (device == NULL) {
    return SIT5156_STATUS_BAD_ARG;
  }
  if (!device->initialized) {
    return SIT5156_STATUS_NOT_INITIALIZED;
  }

  status = ppb_to_control_word(ppb, &control_word);
  if (status != SIT5156_STATUS_OK) {
    return status;
  }

  status = write_control_word(device, control_word);
  if (status == SIT5156_STATUS_OK) {
    device->snapshot.pull_ppb = ppb;
  }
  return status;
}

sit5156_status_t sit5156_center(sit5156_device_t *device)
{
  return sit5156_set_pull_ppb(device, 0);
}

static bool platform_is_valid(const sit5156_platform_t *platform)
{
  return (platform != NULL) &&
         (platform->write != NULL) &&
         (platform->read != NULL) &&
         (platform->delay_us != NULL);
}

static sit5156_status_t read_snapshot(
    const sit5156_device_t *device,
    sit5156_snapshot_t *snapshot)
{
  sit5156_status_t status;

  status = read_register(
      device,
      SIT5156_REG_FREQUENCY_LSW,
      &snapshot->frequency_lsw);
  if (status != SIT5156_STATUS_OK) {
    return status;
  }
  status = read_register(
      device,
      SIT5156_REG_FREQUENCY_MSW_OE,
      &snapshot->frequency_msw_oe);
  if (status != SIT5156_STATUS_OK) {
    return status;
  }
  status = read_register(
      device,
      SIT5156_REG_PULL_RANGE,
      &snapshot->pull_range_register);
  if (status != SIT5156_STATUS_OK) {
    return status;
  }

  decode_frequency(snapshot);
  if (((snapshot->frequency_msw_oe & SIT5156_MSW_RESERVED_MASK) != 0U) ||
      ((snapshot->pull_range_register & SIT5156_PULL_RESERVED_MASK) != 0U) ||
      ((snapshot->pull_range_register & SIT5156_PULL_RANGE_MASK) !=
       SIT5156_PULL_RANGE_CODE)) {
    return SIT5156_STATUS_UNEXPECTED_CONFIG;
  }
  return SIT5156_STATUS_OK;
}

static sit5156_status_t read_register(
    const sit5156_device_t *device,
    uint8_t reg_address,
    uint16_t *value)
{
  uint8_t data[2];

  if ((device == NULL) || (device->platform == NULL) || (value == NULL)) {
    return SIT5156_STATUS_BAD_ARG;
  }
  if (!device->platform->read(reg_address, data, sizeof(data))) {
    return SIT5156_STATUS_IO_ERROR;
  }

  *value = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
  return SIT5156_STATUS_OK;
}

static sit5156_status_t write_register(
    const sit5156_device_t *device,
    uint8_t reg_address,
    uint16_t value)
{
  const uint8_t data[2] = {
    (uint8_t)(value >> 8U),
    (uint8_t)value,
  };

  if ((device == NULL) || (device->platform == NULL)) {
    return SIT5156_STATUS_BAD_ARG;
  }
  return device->platform->write(reg_address, data, sizeof(data))
             ? SIT5156_STATUS_OK
             : SIT5156_STATUS_IO_ERROR;
}

static sit5156_status_t ppb_to_control_word(
    int32_t ppb,
    int32_t *control_word)
{
  int64_t numerator;
  int64_t denominator;

  if (control_word == NULL) {
    return SIT5156_STATUS_BAD_ARG;
  }
  if ((ppb > SIT5156_MAX_PULL_PPB) ||
      (ppb < -SIT5156_MAX_PULL_PPB)) {
    return SIT5156_STATUS_OUT_OF_RANGE;
  }

  numerator = (int64_t)ppb * SIT5156_CONTROL_WORD_LIMIT;
  denominator = (int64_t)SIT5156_PULL_RANGE_PPM * INT64_C(1000);
  *control_word = (int32_t)divide_rounded(numerator, denominator);
  return SIT5156_STATUS_OK;
}

static sit5156_status_t write_control_word(
    sit5156_device_t *device,
    int32_t control_word)
{
  uint8_t data[4];
  uint32_t raw;
  uint16_t expected_lsw;
  uint16_t expected_msw;
  uint16_t readback_lsw;
  uint16_t readback_msw;
  sit5156_status_t status;

  raw = (uint32_t)control_word & SIT5156_CONTROL_WORD_MASK;
  expected_lsw = (uint16_t)raw;
  expected_msw = (uint16_t)(
      ((raw >> 16U) & SIT5156_FREQUENCY_MSW_MASK) |
      (device->snapshot.frequency_msw_oe & SIT5156_OE_MASK));

  /* Auto-increment writes LSW first; loading MSW applies the new frequency. */
  data[0] = (uint8_t)(expected_lsw >> 8U);
  data[1] = (uint8_t)expected_lsw;
  data[2] = (uint8_t)(expected_msw >> 8U);
  data[3] = (uint8_t)expected_msw;
  if (!device->platform->write(
          SIT5156_REG_FREQUENCY_LSW,
          data,
          sizeof(data))) {
    return SIT5156_STATUS_IO_ERROR;
  }
  device->platform->delay_us(SIT5156_FREQUENCY_SETTLE_US);

  status = read_register(
      device,
      SIT5156_REG_FREQUENCY_LSW,
      &readback_lsw);
  if (status != SIT5156_STATUS_OK) {
    return status;
  }
  status = read_register(
      device,
      SIT5156_REG_FREQUENCY_MSW_OE,
      &readback_msw);
  if (status != SIT5156_STATUS_OK) {
    return status;
  }

  device->snapshot.frequency_lsw = readback_lsw;
  device->snapshot.frequency_msw_oe = readback_msw;
  decode_frequency(&device->snapshot);

  if ((readback_lsw != expected_lsw) ||
      ((readback_msw & SIT5156_FREQUENCY_REGISTER_MASK) != expected_msw)) {
    return SIT5156_STATUS_VERIFY_FAILED;
  }
  return SIT5156_STATUS_OK;
}

static void decode_frequency(sit5156_snapshot_t *snapshot)
{
  snapshot->frequency_control_word = decode_control_word(
      snapshot->frequency_lsw,
      snapshot->frequency_msw_oe);
  snapshot->pull_ppb = control_word_to_ppb(
      snapshot->frequency_control_word);
  snapshot->output_enabled =
      (snapshot->frequency_msw_oe & SIT5156_OE_MASK) != 0U;
}

static int32_t decode_control_word(uint16_t lsw, uint16_t msw_oe)
{
  uint32_t raw =
      (((uint32_t)msw_oe & SIT5156_FREQUENCY_MSW_MASK) << 16U) |
      lsw;

  if ((raw & SIT5156_CONTROL_WORD_SIGN_BIT) != 0U) {
    return (int32_t)(raw - (SIT5156_CONTROL_WORD_MASK + UINT32_C(1)));
  }
  return (int32_t)raw;
}

static int32_t control_word_to_ppb(int32_t control_word)
{
  int64_t numerator =
      (int64_t)control_word * SIT5156_PULL_RANGE_PPM * INT64_C(1000);

  return (int32_t)divide_rounded(numerator, SIT5156_CONTROL_WORD_LIMIT);
}

static int64_t divide_rounded(int64_t numerator, int64_t denominator)
{
  int64_t half = denominator / 2;

  if (numerator >= 0) {
    return (numerator + half) / denominator;
  }
  return -((-numerator + half) / denominator);
}
