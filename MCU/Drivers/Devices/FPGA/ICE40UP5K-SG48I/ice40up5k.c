/**
  ******************************************************************************
  * @file           : ice40up5k.c
  * @brief          : iCE40UP5K-SG48I SPI configuration driver
  ******************************************************************************
  */

#include "ice40up5k.h"

#include <string.h>

/* Conservative microsecond delays over the sub-microsecond protocol minima. */
#define ICE40UP5K_CRESET_LOW_US         UINT32_C(200)
#define ICE40UP5K_CDONE_SETTLE_US       UINT32_C(100)
#define ICE40UP5K_CLEAR_MEMORY_US       UINT32_C(2000)

#define ICE40UP5K_LEADING_DUMMY_BYTES   ((size_t)1)
#define ICE40UP5K_CDONE_MAX_BYTES       UINT32_C(32)
#define ICE40UP5K_TRAILING_DUMMY_BYTES  ((size_t)7)
#define ICE40UP5K_HEADER_SEARCH_BYTES   UINT32_C(1024)
#define ICE40UP5K_IMAGE_MIN_BYTES       UINT32_C(1024)

static const uint8_t s_radiant_part_marker[] = "Part: iCE40UP5K-SG48";
static const uint8_t s_sync_word[] = {0x7EU, 0xAAU, 0x99U, 0x7EU};
static const uint8_t s_dummy[ICE40UP5K_TRAILING_DUMMY_BYTES] = {
  0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
};

static bool platform_is_valid(const ice40up5k_platform_t *platform);
static uint32_t find_bytes(
    const uint8_t *data,
    uint32_t data_length,
    const uint8_t *pattern,
    size_t pattern_length);
static void park_in_reset(const ice40up5k_platform_t *platform);
static ice40up5k_status_t send_image(
    const ice40up5k_platform_t *platform,
    const ice40up5k_image_t *image);
static ice40up5k_status_t send_dummy(
    const ice40up5k_platform_t *platform,
    size_t length);

ice40up5k_status_t ice40up5k_validate_image(
    const ice40up5k_image_t *image,
    uint32_t *sync_offset)
{
  uint32_t search_length;
  uint32_t part_offset;
  uint32_t found_sync_offset;

  if (sync_offset != NULL) {
    *sync_offset = 0U;
  }
  if ((image == NULL) || (image->data == NULL)) {
    return ICE40UP5K_STATUS_BAD_ARG;
  }
  if (image->length < ICE40UP5K_IMAGE_MIN_BYTES) {
    return ICE40UP5K_STATUS_BAD_IMAGE;
  }

  search_length = (image->length < ICE40UP5K_HEADER_SEARCH_BYTES)
      ? image->length
      : ICE40UP5K_HEADER_SEARCH_BYTES;
  part_offset = find_bytes(
      image->data,
      search_length,
      s_radiant_part_marker,
      sizeof(s_radiant_part_marker) - 1U);
  found_sync_offset = find_bytes(
      image->data,
      search_length,
      s_sync_word,
      sizeof(s_sync_word));
  if ((part_offset == UINT32_MAX) ||
      (found_sync_offset == UINT32_MAX) ||
      (part_offset >= found_sync_offset)) {
    return ICE40UP5K_STATUS_BAD_IMAGE;
  }

  if (sync_offset != NULL) {
    *sync_offset = found_sync_offset;
  }
  return ICE40UP5K_STATUS_OK;
}

ice40up5k_status_t ice40up5k_configure(
    ice40up5k_result_t *result,
    const ice40up5k_platform_t *platform,
    const ice40up5k_image_t *image)
{
  ice40up5k_status_t status;
  uint32_t cdone_clocks = 0U;
  bool cdone_high = false;

  if (result == NULL) {
    return ICE40UP5K_STATUS_BAD_ARG;
  }
  *result = (ice40up5k_result_t){0};
  if ((image == NULL) || !platform_is_valid(platform)) {
    return ICE40UP5K_STATUS_BAD_ARG;
  }

  status = ice40up5k_validate_image(image, NULL);
  if (status != ICE40UP5K_STATUS_OK) {
    return status;
  }

  /* SPI_SS must be Low when CRESET_B is released to select SPI peripheral mode. */
  if (!platform->spi_select()) {
    park_in_reset(platform);
    return ICE40UP5K_STATUS_SPI_ERROR;
  }
  platform->set_creset_b(false);
  platform->delay_us(ICE40UP5K_CRESET_LOW_US);

  result->cdone_at_reset = platform->read_cdone();
  if (result->cdone_at_reset) {
    park_in_reset(platform);
    return ICE40UP5K_STATUS_CDONE_STUCK_HIGH;
  }

  platform->set_creset_b(true);
  platform->delay_us(ICE40UP5K_CLEAR_MEMORY_US);

  /* Eight clocks with SPI_SS High precede the selected image transfer. */
  platform->spi_deselect();
  status = send_dummy(platform, ICE40UP5K_LEADING_DUMMY_BYTES);
  if (status != ICE40UP5K_STATUS_OK) {
    park_in_reset(platform);
    return status;
  }

  if (!platform->spi_select()) {
    park_in_reset(platform);
    return ICE40UP5K_STATUS_SPI_ERROR;
  }
  status = send_image(platform, image);
  platform->spi_deselect();
  if (status != ICE40UP5K_STATUS_OK) {
    park_in_reset(platform);
    return status;
  }

  /* Continue clocking with SPI_SS High until CDONE reports image acceptance. */
  for (uint32_t i = 0U; i < ICE40UP5K_CDONE_MAX_BYTES; i++) {
    status = send_dummy(platform, 1U);
    if (status != ICE40UP5K_STATUS_OK) {
      result->cdone_clocks = cdone_clocks;
      park_in_reset(platform);
      return status;
    }

    cdone_clocks += 8U;
    if (platform->read_cdone()) {
      cdone_high = true;
      break;
    }
  }
  result->cdone_clocks = cdone_clocks;

  if (!cdone_high) {
    platform->delay_us(ICE40UP5K_CDONE_SETTLE_US);
    cdone_high = platform->read_cdone();
  }
  if (!cdone_high) {
    park_in_reset(platform);
    return ICE40UP5K_STATUS_CDONE_TIMEOUT;
  }

  /* At least 49 additional clocks release the configured user I/O. */
  status = send_dummy(platform, ICE40UP5K_TRAILING_DUMMY_BYTES);
  if (status != ICE40UP5K_STATUS_OK) {
    park_in_reset(platform);
    return status;
  }

  platform->delay_us(ICE40UP5K_CDONE_SETTLE_US);
  if (!platform->read_cdone()) {
    park_in_reset(platform);
    return ICE40UP5K_STATUS_CDONE_DROPPED;
  }

  return ICE40UP5K_STATUS_OK;
}

static bool platform_is_valid(const ice40up5k_platform_t *platform)
{
  return (platform != NULL) &&
         (platform->set_creset_b != NULL) &&
         (platform->read_cdone != NULL) &&
         (platform->spi_select != NULL) &&
         (platform->spi_deselect != NULL) &&
         (platform->spi_write != NULL) &&
         (platform->delay_us != NULL);
}

static uint32_t find_bytes(
    const uint8_t *data,
    uint32_t data_length,
    const uint8_t *pattern,
    size_t pattern_length)
{
  if ((data == NULL) || (pattern == NULL) || (pattern_length == 0U) ||
      (pattern_length > data_length)) {
    return UINT32_MAX;
  }

  for (uint32_t offset = 0U;
       offset <= data_length - (uint32_t)pattern_length;
       offset++) {
    if (memcmp(&data[offset], pattern, pattern_length) == 0) {
      return offset;
    }
  }
  return UINT32_MAX;
}

static void park_in_reset(const ice40up5k_platform_t *platform)
{
  platform->spi_deselect();
  platform->set_creset_b(false);
}

static ice40up5k_status_t send_image(
    const ice40up5k_platform_t *platform,
    const ice40up5k_image_t *image)
{
  return platform->spi_write(image->data, (size_t)image->length)
             ? ICE40UP5K_STATUS_OK
             : ICE40UP5K_STATUS_SPI_ERROR;
}

static ice40up5k_status_t send_dummy(
    const ice40up5k_platform_t *platform,
    size_t length)
{
  if ((length == 0U) || (length > sizeof(s_dummy))) {
    return ICE40UP5K_STATUS_BAD_ARG;
  }
  return platform->spi_write(s_dummy, length)
             ? ICE40UP5K_STATUS_OK
             : ICE40UP5K_STATUS_SPI_ERROR;
}
