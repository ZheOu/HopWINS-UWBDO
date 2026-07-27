/**
  ******************************************************************************
  * @file           : ice40.c
  * @brief          : Lattice iCE40 SPI slave-mode configuration driver
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "ice40.h"

/* Private define ------------------------------------------------------------*/

/* FPGA-TN-02001 13.2: hold CRESET_B Low for at least 200 ns. */
#define ICE40_CRESET_LOW_US        10U

/* FPGA-TN-02001 13.2: wait at least 1200 us to clear configuration memory. */
#define ICE40_CLEAR_MEMORY_US      2000U

/* FPGA-TN-02001 13.2: 8 dummy clocks with SPI_SS High before the image. */
#define ICE40_LEADING_DUMMY_BYTES  1U

/*
 * FPGA-TN-02001 13.2: allow 100 SPI_SCK cycles for CDONE to rise. Polled one
 * byte at a time so that the actual number of cycles is reported; the limit is
 * deliberately generous so a slow board shows up as a large cdone_clocks value
 * rather than a bare failure.
 */
#define ICE40_CDONE_MAX_BYTES      32U

/* FPGA-TN-02001 13.2: at least 49 further clocks to release the user I/O. */
#define ICE40_TRAILING_DUMMY_BYTES 7U

/* Largest single HAL_SPI_Transmit() burst; the image exceeds UINT16_MAX. */
#define ICE40_SPI_CHUNK_BYTES      1024U

/*
 * Window searched for the synchronisation word, past the ASCII header. The
 * Radiant header is short: in the iCE40UP5K-SG48 test bitstream the word sits
 * at offset 71, behind "\xFF\x00Lattice\0DiamondNG\0Part: ...\0Date: ...\0".
 */
#define ICE40_SYNC_SEARCH_BYTES    1024U

/*
 * Smallest and largest image accepted, guarding against erased flash. For
 * reference, a full iCE40UP5K-SG48 image is 104157 bytes.
 */
#define ICE40_IMAGE_MIN_LEN        1024U
#define ICE40_IMAGE_MAX_LEN        (1024U * 1024U)

/* Erased internal flash reads back as 0xFF. */
#define ICE40_FLASH_ERASED_BYTE    0xFFU

/* Private variables ---------------------------------------------------------*/

static const uint8_t s_dummy[ICE40_TRAILING_DUMMY_BYTES] = {
  0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
};

/* Private function prototypes -----------------------------------------------*/

static bool ice40_platform_is_valid(const ice40_platform_t *platform);
static ice40_status_t ice40_send_image(
    const ice40_platform_t *platform,
    const ice40_image_t *image);
static ice40_status_t ice40_send_dummy(
    const ice40_platform_t *platform,
    uint16_t len);

/* Public functions ----------------------------------------------------------*/

ice40_status_t ice40_check_image(
    const ice40_image_t *image,
    uint32_t *sync_offset)
{
  uint32_t search_len;

  if ((image == NULL) || (image->data == NULL)) {
    return ICE40_STATUS_BAD_ARG;
  }

  if ((image->len < ICE40_IMAGE_MIN_LEN) || (image->len > ICE40_IMAGE_MAX_LEN)) {
    return ICE40_STATUS_BAD_IMAGE;
  }

  search_len = (image->len < ICE40_SYNC_SEARCH_BYTES)
      ? image->len
      : ICE40_SYNC_SEARCH_BYTES;

  /*
   * A Radiant .bin opens with 0xFF 0x00 followed by an ASCII comment block
   * ("Lattice", "Part: ...", "Date: ..."), then the 0x7EAA997E synchronisation
   * word. Erased flash reads back as all 0xFF and never matches.
   */
  for (uint32_t i = 0U; (i + 4U) <= search_len; i++) {
    if ((image->data[i] == 0x7EU) &&
        (image->data[i + 1U] == 0xAAU) &&
        (image->data[i + 2U] == 0x99U) &&
        (image->data[i + 3U] == 0x7EU)) {
      if (sync_offset != NULL) {
        *sync_offset = i;
      }
      return ICE40_STATUS_OK;
    }
  }

  return ICE40_STATUS_BAD_IMAGE;
}

ice40_status_t ice40_image_from_flash(
    const uint8_t *region,
    uint32_t region_len,
    ice40_image_t *image)
{
  uint32_t len = region_len;

  if ((region == NULL) || (image == NULL)) {
    return ICE40_STATUS_BAD_ARG;
  }

  /*
   * The image is programmed into the region on its own, so nothing records how
   * long it is. Erased flash reads back as 0xFF and a Radiant image always ends
   * on the wake-up command instead (the test bitstream ends 01 06 00), so the
   * last byte that is not 0xFF is the last byte of the image.
   */
  while ((len > 0U) && (region[len - 1U] == ICE40_FLASH_ERASED_BYTE)) {
    len--;
  }

  image->data = region;
  image->len = len;

  return ice40_check_image(image, NULL);
}

ice40_status_t ice40_configure(
    ice40_device_t *device,
    const ice40_platform_t *platform,
    const ice40_image_t *image)
{
  ice40_status_t status;
  uint32_t sync_offset = 0U;
  uint32_t cdone_clocks = 0U;
  bool cdone_high = false;

  if ((device == NULL) || (image == NULL) || !ice40_platform_is_valid(platform)) {
    return ICE40_STATUS_BAD_ARG;
  }

  device->configured = false;
  device->image_len = image->len;
  device->sync_offset = 0U;
  device->cdone_clocks = 0U;

  status = ice40_check_image(image, &sync_offset);
  if (status != ICE40_STATUS_OK) {
    return status;
  }
  device->sync_offset = sync_offset;

  /* Step 1/2: CRESET_B Low and SPI_SS Low. */
  platform->spi_deselect();
  platform->set_creset_b(false);
  platform->spi_select();

  /* Step 3: hold reset for at least 200 ns. */
  platform->delay_us(ICE40_CRESET_LOW_US);

  /*
   * While CRESET_B is Low the FPGA must be driving CDONE Low. Reading it High
   * here means CRESET_B or CDONE is not actually connected to the FPGA, which
   * is worth catching before a full image is clocked out into nothing.
   */
  if (platform->get_cdone()) {
    platform->spi_deselect();
    platform->set_creset_b(true);
    return ICE40_STATUS_CDONE_STUCK_HIGH;
  }

  /* Step 4: release CRESET_B with SPI_SS still Low to enter SPI peripheral mode. */
  platform->set_creset_b(true);

  /* Step 5: wait for the internal configuration memory to clear. */
  platform->delay_us(ICE40_CLEAR_MEMORY_US);

  /* Step 6: SPI_SS High, 8 dummy clocks. */
  platform->spi_deselect();
  status = ice40_send_dummy(platform, ICE40_LEADING_DUMMY_BYTES);
  if (status != ICE40_STATUS_OK) {
    return status;
  }

  /* Step 7: SPI_SS Low for the whole image, MSB first, no chip-select glitch. */
  platform->spi_select();
  status = ice40_send_image(platform, image);
  platform->spi_deselect();
  if (status != ICE40_STATUS_OK) {
    return status;
  }

  /* Step 8: clock until CDONE floats High, within roughly 100 SPI_SCK cycles. */
  for (uint32_t i = 0U; i < ICE40_CDONE_MAX_BYTES; i++) {
    status = ice40_send_dummy(platform, 1U);
    if (status != ICE40_STATUS_OK) {
      device->cdone_clocks = cdone_clocks;
      return status;
    }

    cdone_clocks += 8U;
    if (platform->get_cdone()) {
      cdone_high = true;
      break;
    }
  }

  device->cdone_clocks = cdone_clocks;

  if (!cdone_high) {
    return ICE40_STATUS_CDONE_TIMEOUT;
  }

  /* Step 9: at least 49 more clocks so the user I/O go live. */
  status = ice40_send_dummy(platform, ICE40_TRAILING_DUMMY_BYTES);
  if (status != ICE40_STATUS_OK) {
    return status;
  }

  device->configured = true;
  return ICE40_STATUS_OK;
}

bool ice40_is_configured(const ice40_device_t *device)
{
  return (device != NULL) && device->configured;
}

/* Private functions ---------------------------------------------------------*/

static bool ice40_platform_is_valid(const ice40_platform_t *platform)
{
  return (platform != NULL) &&
         (platform->set_creset_b != NULL) &&
         (platform->get_cdone != NULL) &&
         (platform->spi_select != NULL) &&
         (platform->spi_deselect != NULL) &&
         (platform->spi_write != NULL) &&
         (platform->delay_us != NULL);
}

static ice40_status_t ice40_send_image(
    const ice40_platform_t *platform,
    const ice40_image_t *image)
{
  uint32_t offset = 0U;

  /*
   * The image is longer than the 16-bit HAL transfer length, so it is split
   * into chunks. SPI_SS stays Low across the whole loop: the FPGA only advances
   * on SPI_SCK edges, so the short idle gaps between chunks are harmless.
   */
  while (offset < image->len) {
    uint32_t remaining = image->len - offset;
    uint16_t chunk = (remaining > ICE40_SPI_CHUNK_BYTES)
        ? (uint16_t)ICE40_SPI_CHUNK_BYTES
        : (uint16_t)remaining;

    if (platform->spi_write(&image->data[offset], chunk) != 0) {
      return ICE40_STATUS_SPI_ERROR;
    }

    offset += chunk;
  }

  return ICE40_STATUS_OK;
}

static ice40_status_t ice40_send_dummy(
    const ice40_platform_t *platform,
    uint16_t len)
{
  if ((len == 0U) || (len > (uint16_t)sizeof(s_dummy))) {
    return ICE40_STATUS_BAD_ARG;
  }

  if (platform->spi_write(s_dummy, len) != 0) {
    return ICE40_STATUS_SPI_ERROR;
  }

  return ICE40_STATUS_OK;
}
