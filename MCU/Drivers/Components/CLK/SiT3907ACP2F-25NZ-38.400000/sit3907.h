/**
  ******************************************************************************
  * @file           : sit3907.h
  * @brief          : SiTime SiT3907 digitally controlled oscillator (DCXO) driver
  *
  * Pulls the oscillator frequency around its factory-programmed nominal value.
  * This is the actuator of the distributed-oscillator loop: UWB reception
  * measures the frequency error against the remote node, and this driver
  * corrects the local oscillator.
  *
  * Part fitted on this board: SiT3907ACP2F-25NZ-38.400000
  *
  *   nominal frequency     38.4 MHz, the board reference
  *   pull range            +/-1600 ppm      -> SIT3907_PULL_RANGE_PPM
  *   frequency stability   +/-10 ppm
  *   supply                2.5 V
  *   package               3.2 x 2.5 mm, 4-pin (pin 1 = DP, pin 4 = VDD)
  *   operating range       -20 to +70 C
  *
  * At 2.5 V supply the DP input thresholds work out to VIL <= 0.5 V,
  * VIM 1.0 to 1.5 V, VIH >= 2.0 V. Check the level translation between the MCU
  * I/O rail and this pin before trusting a bench result: an MCU driving 1.8 V
  * lands between VIM and VIH, in the undefined band, where a "1" is not
  * guaranteed to be seen as a "1".
  *
  * Interface (SiT3907 rev 1.2 datasheet, "Physical Interface")
  * ----------------------------------------------------------
  * One wire, tri-level, return-to-middle. A single pin (DP) carries everything:
  *
  *   bit "1" = drive High for T_logic, then release to mid for T_middle
  *   bit "0" = drive Low  for T_logic, then release to mid for T_middle
  *
  * The mid level is produced by tri-stating the pin; an external divider pulls
  * it to VIM (0.4..0.6 x Vdd). T_logic and T_middle have a 500 ns MINIMUM and
  * no maximum, so a bit-banged GPIO needs no tight timing and overshooting is
  * always safe. The line rests at mid between frames.
  *
  * On HopWINS-UWBDO-PCB-STDWDO the DP pin is GPIO_CLKDP_Pin, and
  * board_clkdp_set_mode() already provides exactly the three levels needed.
  *
  * Frame format (datasheet "Frequency Control Protocol Description")
  * ----------------------------------------------------------------
  * 40 bits, most significant bit first:
  *
  *   | header 16 bits | register 8 bits | pull-frequency value 16 bits |
  *   |     0xFAxA     |  0x06 or 0x07   |      2's complement          |
  *
  * The header's third nibble is the factory-programmed device address, zero
  * unless ordered otherwise, giving 0xFA0A.
  *
  * Mode 1  one frame to 0x06 with a 16-bit value. Frequency updates at the end
  *         of the frame.
  * Mode 2  23-bit value split across two frames. 0x07 carries the LOW 7 bits
  *         and is sent first (no frequency change); 0x06 carries the UPPER 16
  *         bits and is sent second, which is when the frequency updates.
  *
  * Note the datasheet contradicts itself here: the paragraph beside Figure 2
  * claims 0x07 carries the most significant 7 bits, while the "Calculating Pull
  * Frequency Values" section and both worked examples say the upper 16 bits go
  * to 0x06. The examples were checked numerically and the section is right:
  *
  *   +245.6 ppm, PR = 1600, mode 2 -> code 642954 = 0x09CF8A
  *       0x06 <- 642954 >> 7   = 0x139F   (datasheet "MS Word")
  *       0x07 <- 642954 & 0x7F = 0x000A   (datasheet "LS Word")
  *
  * Pull value scaling (datasheet "Calculating Pull Frequency Values")
  * -----------------------------------------------------------------
  *   K    = full_scale / (pull_range_ppm * 1.00135625)
  *   code = round(desired_ppm * K)
  *
  * with full_scale = 2^15-1 in mode 1 and 2^22-1 in mode 2. This driver works
  * in parts per billion so the whole conversion stays in integer arithmetic;
  * 1 ppb is also the finest step the part offers.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef HOPWINS_SIT3907_H
#define HOPWINS_SIT3907_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
  * @brief Pull range of the part on this board, in ppm.
  *
  * Every ppb-to-code conversion scales by this. Getting it wrong does not fail,
  * it silently scales every correction: telling a +/-100 ppm part that it is a
  * +/-1600 ppm part makes a commanded 1 ppm come out as 0.0625 ppm. It comes
  * from the ordering code, "-25NZ" here, not from anything readable at runtime.
  */
#define SIT3907_PULL_RANGE_PPM 1600U

/** @brief Nominal output frequency of the fitted part, in Hz. */
#define SIT3907_NOMINAL_HZ     UINT32_C(38400000)

typedef enum {
  SIT3907_STATUS_OK = 0,
  SIT3907_STATUS_BAD_ARG = -1,
  SIT3907_STATUS_NOT_INITIALIZED = -2,
  /** Requested pull exceeds the part's programmed pull range. */
  SIT3907_STATUS_OUT_OF_RANGE = -3,
} sit3907_status_t;

typedef enum {
  /** One frame, 16-bit value. Coarser step, 25 k updates per second. */
  SIT3907_MODE_1 = 1,
  /** Two frames, 23-bit value. 1 ppb step, 12.5 k updates per second. */
  SIT3907_MODE_2 = 2,
} sit3907_mode_t;

/**
  * @brief Platform hooks supplied by the board or service layer.
  *
  * The three level functions must drive the DP pin as named; release() has to
  * put the pin in a high-impedance state so the external divider can pull it to
  * the mid level, because mid is what terminates every bit.
  *
  * delay_us() only has to guarantee AT LEAST the requested time. The datasheet
  * specifies minimums for T_logic and T_middle and no maximums, so a coarse or
  * overshooting delay costs baud rate and nothing else.
  */
typedef struct sit3907_platform {
  void (*dp_drive_high)(void);
  void (*dp_drive_low)(void);
  void (*dp_release)(void);
  void (*delay_us)(uint32_t delay_us);
} sit3907_platform_t;

typedef struct sit3907_config {
  /** Factory-programmed device address, third nibble of the header. 0..15. */
  uint8_t device_address;
  /** Programmed pull range in ppm: 25, 50, 100, 200, 400, 800 or 1600. */
  uint16_t pull_range_ppm;
  sit3907_mode_t mode;
  /** High/low level hold time. Clamped up to the 500 ns datasheet minimum. */
  uint32_t t_logic_us;
  /**
    * Mid level hold time. Also clamped to 500 ns, but worth leaving longer than
    * the minimum: the mid level is an RC settle through the external divider,
    * and the datasheet asks for enough time to land within about 5 %.
    */
  uint32_t t_middle_us;
} sit3907_config_t;

typedef struct sit3907_device {
  bool initialized;
  sit3907_config_t config;
  const sit3907_platform_t *platform;
  uint16_t header;
  /** Largest magnitude the configured mode can encode. */
  int32_t code_limit;
  /** Last code written, and the pull it corresponds to. */
  int32_t last_code;
  int32_t last_ppb;
  uint32_t writes;
} sit3907_device_t;

/**
  * @brief Validate the configuration and park the DP pin at the mid level.
  *
  * Does not change the output frequency: the part starts at its factory
  * frequency with both DCXO registers zeroed, which is the centre of the pull
  * range, and this call leaves it there.
  */
sit3907_status_t sit3907_init(
    sit3907_device_t *device,
    const sit3907_platform_t *platform,
    const sit3907_config_t *config);

/**
  * @brief Pull the frequency by @p ppb parts per billion.
  *
  * Positive pulls the frequency up. Rejects values outside the programmed pull
  * range rather than silently saturating, because in a control loop a silent
  * clamp looks like a working actuator that has stopped responding.
  */
sit3907_status_t sit3907_set_pull_ppb(sit3907_device_t *device, int32_t ppb);

/** @brief Write a raw 2's complement code, bypassing the ppb conversion. */
sit3907_status_t sit3907_set_pull_code(sit3907_device_t *device, int32_t code);

/** @brief Return to the centre of the pull range. */
sit3907_status_t sit3907_center(sit3907_device_t *device);

/**
  * @brief Convert a pull in ppb to the code the part expects, no I/O.
  *
  * Returns SIT3907_STATUS_OUT_OF_RANGE without writing @p code if the request
  * does not fit the programmed pull range.
  */
sit3907_status_t sit3907_ppb_to_code(
    const sit3907_device_t *device,
    int32_t ppb,
    int32_t *code);

/** @brief Inverse of sit3907_ppb_to_code(), for reporting what was applied. */
sit3907_status_t sit3907_code_to_ppb(
    const sit3907_device_t *device,
    int32_t code,
    int32_t *ppb);

/** @brief Smallest pull step the configured mode can express, in ppb. */
sit3907_status_t sit3907_step_ppb(const sit3907_device_t *device, int32_t *ppb);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_SIT3907_H */
