/**
  ******************************************************************************
  * @file           : ice40.c
  * @brief          : Lattice iCE40 SPI slave-mode configuration driver
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "ice40.h"

/* Private define ------------------------------------------------------------*/

/*
 * FPGA-TN-02001 13.2 asks for at least 200 ns. CRESET_B is driven open-drain
 * with a pull-up, so the release is an RC edge rather than a driven one; the
 * hold is generous to keep the pulse unambiguous at both ends.
 */
#define ICE40_CRESET_LOW_US        200U

/*
 * CDONE is open-drain with a pull-up, so it rises far more slowly than a
 * push-pull pin. Used before the reads whose answer decides success.
 */
#define ICE40_CDONE_SETTLE_US      100U

/* Time the device stays in reset between two configuration attempts. */
#define ICE40_RETRY_SETTLE_US      1000U

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
static void ice40_park_in_reset(const ice40_platform_t *platform);
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
  device->cdone_at_reset = false;

  status = ice40_check_image(image, &sync_offset);
  if (status != ICE40_STATUS_OK) {
    return status;
  }
  device->sync_offset = sync_offset;

  /*
   * Step 1: SPI_SS Low, before CRESET_B moves at all. The board layer forces
   * the configuration baud rate from inside spi_select(), which can
   * re-initialise SPI1; doing that first keeps a peripheral re-init from
   * landing between the reset pulse and the image.
   */
  platform->spi_select();

  /* Step 2: CRESET_B Low, held far longer than the 200 ns minimum. */
  platform->set_creset_b(false);
  platform->delay_us(ICE40_CRESET_LOW_US);

  /* A live device must pull CDONE Low while CRESET_B is asserted. */
  device->cdone_at_reset = platform->get_cdone();
  if (device->cdone_at_reset) {
    /*
     * A pull-up reads High when the FPGA is absent. Continuing would see that
     * same High after the first dummy byte and falsely report configuration
     * success, even though no device ever acknowledged reset.
     */
    ice40_park_in_reset(platform);
    return ICE40_STATUS_CDONE_STUCK_HIGH;
  }

  /*
   * Step 3: the CRESET_B rising edge WHILE SPI_SS IS STILL LOW is what selects
   * SPI peripheral mode. With SPI_SS High here the device would come up as an
   * SPI master looking for a boot Flash this board does not have.
   */
  platform->set_creset_b(true);

  /* Step 4: wait for the internal configuration memory to clear. */
  platform->delay_us(ICE40_CLEAR_MEMORY_US);

  /* Step 5: SPI_SS High, 8 dummy clocks. */
  platform->spi_deselect();
  status = ice40_send_dummy(platform, ICE40_LEADING_DUMMY_BYTES);
  if (status != ICE40_STATUS_OK) {
    ice40_park_in_reset(platform);
    return status;
  }

  /* Step 6: SPI_SS Low for the whole image, MSB first, no chip-select glitch. */
  platform->spi_select();
  status = ice40_send_image(platform, image);
  platform->spi_deselect();
  if (status != ICE40_STATUS_OK) {
    ice40_park_in_reset(platform);
    return status;
  }

  /* Step 7: clock with SPI_SS High until CDONE rises, about 100 SPI_SCK cycles. */
  for (uint32_t i = 0U; i < ICE40_CDONE_MAX_BYTES; i++) {
    status = ice40_send_dummy(platform, 1U);
    if (status != ICE40_STATUS_OK) {
      device->cdone_clocks = cdone_clocks;
      ice40_park_in_reset(platform);
      return status;
    }

    cdone_clocks += 8U;
    if (platform->get_cdone()) {
      cdone_high = true;
      break;
    }
  }

  device->cdone_clocks = cdone_clocks;

  /*
   * CDONE is an open-drain output pulled up by a resistor, so its rising edge
   * is slow next to a byte time. One settled re-read before giving up keeps a
   * still-rising edge from being called a timeout.
   */
  if (!cdone_high) {
    platform->delay_us(ICE40_CDONE_SETTLE_US);
    cdone_high = platform->get_cdone();
  }

  if (!cdone_high) {
    ice40_park_in_reset(platform);
    return ICE40_STATUS_CDONE_TIMEOUT;
  }

  /* Step 8: at least 49 more clocks so the user I/O go live. */
  status = ice40_send_dummy(platform, ICE40_TRAILING_DUMMY_BYTES);
  if (status != ICE40_STATUS_OK) {
    ice40_park_in_reset(platform);
    return status;
  }

  /*
   * Re-read CDONE now the device should be in user mode. A device whose
   * internal CRC check fails pulls CDONE back Low here, which would otherwise
   * be reported as success.
   */
  platform->delay_us(ICE40_CDONE_SETTLE_US);
  if (!platform->get_cdone()) {
    ice40_park_in_reset(platform);
    return ICE40_STATUS_CDONE_DROPPED;
  }

  device->configured = true;
  return ICE40_STATUS_OK;
}

ice40_status_t ice40_configure_retry(
    ice40_device_t *device,
    const ice40_platform_t *platform,
    const ice40_image_t *image,
    uint32_t max_attempts)
{
  ice40_status_t status = ICE40_STATUS_BAD_ARG;
  uint32_t attempts = (max_attempts == 0U) ? 1U : max_attempts;

  if (device == NULL) {
    return ICE40_STATUS_BAD_ARG;
  }

  device->attempts = 0U;

  for (uint32_t i = 0U; i < attempts; i++) {
    /* Each attempt is a whole sequence; configuration cannot resume midway. */
    status = ice40_configure(device, platform, image);
    device->attempts = i + 1U;

    if (status == ICE40_STATUS_OK) {
      return status;
    }

    /* Neither the image nor the arguments change between attempts. */
    if ((status == ICE40_STATUS_BAD_ARG) ||
        (status == ICE40_STATUS_BAD_IMAGE) ||
        (status == ICE40_STATUS_CDONE_STUCK_HIGH)) {
      return status;
    }

    /* ice40_configure() left the device in reset; let it settle first. */
    if ((i + 1U) < attempts) {
      platform->delay_us(ICE40_RETRY_SETTLE_US);
    }
  }

  return status;
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

/*
 * Safe state after a failed attempt: SPI_SS released so the DW3000 can use the
 * shared bus, and CRESET_B asserted so the FPGA keeps its I/O tri-stated rather
 * than running a half-loaded configuration. It is also where the next attempt
 * expects to start.
 */
static void ice40_park_in_reset(const ice40_platform_t *platform)
{
  platform->spi_deselect();
  platform->set_creset_b(false);
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
