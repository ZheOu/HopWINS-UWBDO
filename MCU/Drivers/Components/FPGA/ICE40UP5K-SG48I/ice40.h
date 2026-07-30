/**
  ******************************************************************************
  * @file           : ice40.h
  * @brief          : Lattice iCE40 SPI slave-mode configuration driver
  *
  * Implements the configuration sequence of FPGA-TN-02001 (iCE40 Programming
  * and Configuration), section 13.2 "SPI Slave Configuration Process".
  *
  * The application processor drives CRESET_B and SPI_SS as GPIO and clocks the
  * configuration image out on the SPI bus. The iCE40 captures SPI_SI on the
  * rising edge of SPI_SCK, so the bus must run in SPI mode 0 or mode 3, MSB
  * first, at 1 MHz to 25 MHz.
  *
  * Signals used, named as in HopWINS-UWBDO-MCU.ioc / main.h:
  *
  *   iCE40 function  MCU pin macro       configured as
  *   --------------- ------------------- ------------------------------
  *   CRESET_B        GPIO_FPGARST_Pin    open drain, pull-up
  *   CDONE           GPIO_FPGADONE_Pin   input, pull-up
  *   SPI_SS          SPI1_CSFPGA_Pin     push-pull output
  *   SPI_SCK/SI/SO   SPI1 alternate function pins
  *
  * Both CRESET_B and CDONE are open drain against a pull-up, so every edge on
  * them is an RC ramp rather than a driven transition. Anything this driver
  * decides on has a settling delay in front of it for that reason.
  *
  * GPIO_FPGAEN_Pin takes no part in configuration; it is the MCU-to-FPGA link
  * the configured design uses afterwards, and this driver never touches it.
  *
  * SPI1 is shared with the DW3000, which has its own chip select, so SPI_SS
  * must be the only select asserted for the whole sequence.
  *
  * Required order (FPGA-TN-02001 13.2). Releasing CRESET_B while SPI_SS is High
  * brings the device up as an SPI master instead, hunting for a boot Flash this
  * board does not have, and CDONE then never rises:
  *
  *   1. SPI_SS Low
  *   2. CRESET_B Low, held at least 200 ns    <- CDONE goes Low here
  *   3. CRESET_B released High, SPI_SS STILL LOW  <- latches slave mode
  *   4. wait at least 1200 us for the configuration memory to clear
  *   5. SPI_SS High, 8 dummy clocks
  *   6. SPI_SS Low, whole image MSB first, no chip-select glitch
  *   7. SPI_SS High, clock until CDONE rises (about 100 SPI_SCK cycles)
  *   8. at least 49 further clocks to release the user I/O
  *
  * The MCU build embeds the configuration binary in the FPGA_IMAGE linker
  * region at 0x081E0000 (128 KB). The linker exports
  * __fpga_image_start__/__fpga_image_end__ for the exact image bounds and
  * __fpga_image_capacity__ for the reserved region size. The image is read back
  * directly through the STM32 memory-mapped Flash interface.
  *
  * CDONE is the only configuration result the MCU can observe: FPGA_CLKOUT
  * (pin 21) only reaches the IPEX test connector, and PA0/TIM2_ETR is fed by
  * the clock buffer rather than by the FPGA.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef HOPWINS_ICE40_H
#define HOPWINS_ICE40_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  ICE40_STATUS_OK = 0,
  ICE40_STATUS_BAD_ARG = -1,
  ICE40_STATUS_BAD_IMAGE = -2,
  ICE40_STATUS_SPI_ERROR = -3,
  /** CDONE stayed High in reset: device absent or CRESET_B/CDONE disconnected. */
  ICE40_STATUS_CDONE_STUCK_HIGH = -4,
  /** CDONE never rose, having correctly been Low while CRESET_B was asserted. */
  ICE40_STATUS_CDONE_TIMEOUT = -5,
  /** CDONE rose but fell again, which is how a failed internal CRC shows up. */
  ICE40_STATUS_CDONE_DROPPED = -6,
} ice40_status_t;

/**
  * @brief Platform hooks supplied by the board layer.
  *
  * spi_write() must clock @p len bytes out of the SPI peripheral WITHOUT
  * touching the chip select, because the whole configuration image has to be
  * sent inside a single SPI_SS low window, and the dummy clocks have to be sent
  * with SPI_SS high.
  *
  * set_creset_b() takes the pin level, not the reset state: CRESET_B is active
  * low, so false asserts reset and true releases it.
  *
  * spi_select() is also the place to force the SPI baud rate into the 1 MHz to
  * 25 MHz configuration window, because the DW3000 driver sharing the bus
  * leaves its own rate behind.
  */
typedef struct ice40_platform {
  void (*set_creset_b)(bool high);
  bool (*get_cdone)(void);
  void (*spi_select)(void);
  void (*spi_deselect)(void);
  int32_t (*spi_write)(const uint8_t *data, uint16_t len);
  void (*delay_us)(uint32_t delay_us);
} ice40_platform_t;

/** @brief Configuration image held in memory-mapped flash. */
typedef struct ice40_image {
  const uint8_t *data;
  uint32_t len;
} ice40_image_t;

typedef struct ice40_device {
  bool configured;
  uint32_t image_len;
  uint32_t sync_offset;  /**< Offset of the 0x7EAA997E synchronisation word. */
  uint32_t cdone_clocks; /**< SPI_SCK cycles sent after the image until CDONE. */
  uint32_t attempts;     /**< Sequences run, including the one that succeeded. */
  /**
    * CDONE sampled while CRESET_B was still asserted on the last attempt. A
    * live device holds it Low there; High means the FPGA is absent or
    * CRESET_B/CDONE is not reaching it, and configuration is aborted.
    */
  bool cdone_at_reset;
} ice40_device_t;

/**
  * @brief Sanity-check a configuration image before sending it.
  *
  * Rejects erased flash and truncated images by locating the 0x7EAA997E
  * synchronisation pattern that follows the ASCII comment header.
  */
ice40_status_t ice40_check_image(
    const ice40_image_t *image,
    uint32_t *sync_offset);

/**
  * @brief Describe an image programmed on its own into memory-mapped flash.
  *
  * @p region_len is the size of the reserved flash region, not of the image.
  * The image length is recovered by discarding the erased tail of the region,
  * then the result is validated with ice40_check_image().
  */
ice40_status_t ice40_image_from_flash(
    const uint8_t *region,
    uint32_t region_len,
    ice40_image_t *image);

/**
  * @brief Run the SPI slave configuration sequence once.
  *
  * On success the FPGA is in user mode and the SPI bus is free for other
  * peripherals again. On failure the device is parked with CRESET_B asserted
  * and SPI_SS released, which is the state a retry starts from.
  */
ice40_status_t ice40_configure(
    ice40_device_t *device,
    const ice40_platform_t *platform,
    const ice40_image_t *image);

/**
  * @brief Retry the sequence until CDONE rises or the attempts run out.
  *
  * Faults a retry cannot clear, a rejected image or a bad argument, return
  * immediately. @p max_attempts is clamped to at least one, and the count
  * actually used is left in device->attempts.
  */
ice40_status_t ice40_configure_retry(
    ice40_device_t *device,
    const ice40_platform_t *platform,
    const ice40_image_t *image,
    uint32_t max_attempts);

bool ice40_is_configured(const ice40_device_t *device);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_ICE40_H */
