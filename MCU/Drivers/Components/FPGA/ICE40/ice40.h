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
  * Wiring on HopWINS-UWBDO-PCB-STDWDO (iCE40UP5K-SG48I, from Schematic.pdf):
  *
  *   iCE40 pin            net            STM32U585 pin
  *   -------------------- -------------- ---------------------
  *   7  CDONE             FPGA_DONE      PC14   (GPIO input)
  *   8  CRESET_B          FPGA_RST       PH0    (GPIO output)
  *   14 IOB_32a_SPI_SO    FPGA_SPIMISO   PA6    (SPI1_MISO)
  *   15 IOB_34a_SPI_SCK   FPGA_SPICLK    PA5    (SPI1_SCK)
  *   16 IOB_35b_SPI_SS    FPGA_SPICS     PA4    (GPIO output)
  *   17 IOB_33b_SPI_SI    FPGA_SPIMOSI   PA7    (SPI1_MOSI)
  *
  * SPI1 is shared with the DW3000, which has its own chip select on PA3, so
  * SPI_SS must be the only select asserted for the whole sequence. Bank 1 of
  * the FPGA (SPI_VCCIO1, pin 22) and the MCU both run from the 1V8 rail, so
  * the bus is a direct connection with no level shifting.
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
  ICE40_STATUS_CDONE_STUCK_HIGH = -4,
  ICE40_STATUS_CDONE_TIMEOUT = -5,
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
  * @brief Run the full SPI slave configuration sequence.
  *
  * On success the FPGA is in user mode and the SPI bus is free for other
  * peripherals again.
  */
ice40_status_t ice40_configure(
    ice40_device_t *device,
    const ice40_platform_t *platform,
    const ice40_image_t *image);

bool ice40_is_configured(const ice40_device_t *device);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_ICE40_H */
