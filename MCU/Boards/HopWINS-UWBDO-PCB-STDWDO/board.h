/**
  ******************************************************************************
  * @file           : board.h
  * @brief          : HopWINS-UWBDO PCB population and board-level interfaces
  ******************************************************************************
  */

#ifndef HOPWINS_BOARD_H
#define HOPWINS_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct dw3000_platform;
struct ice40up5k_platform;

/** Result returned by a Board-layer operation. */
typedef enum {
  BOARD_OK = 0,
  BOARD_ERROR = -1,
  BOARD_TIMEOUT = -2,
  BOARD_BAD_ARG = -3,
  BOARD_BUSY = -4,
  BOARD_UNSUPPORTED = -5,
} board_status_t;

/**
 * Exact BOM populations supported by this PCB definition.
 *
 * Select one value in `HOPWINS_BOARD_VARIANT` in `Src/main.c`. The value is
 * only a key into the Board's immutable population table; it does not select a
 * workflow or change driver behaviour. `BOARD_VARIANT_COUNT` is the sentinel
 * one past the final valid variant. It sizes that table and validates inputs;
 * it must never be selected as a real board.
 */
typedef enum {
  BOARD_VARIANT_FPGA_NONE_CLK_NONE_UWB_DW3000_DUAL_RF1 = 0,
  BOARD_VARIANT_FPGA_NONE_CLK_DCTCXO_SIT5156_UWB_DW3000_DUAL_RF1,
  BOARD_VARIANT_FPGA_ICE40UP5K_CLK_DCTCXO_SIT5156_UWB_DW3000_DUAL_RF1_RF2,
  BOARD_VARIANT_FPGA_ICE40UP5K_CLK_DCO_SIT3907_UWB_DW3000_DUAL_RF1_RF2,
  BOARD_VARIANT_COUNT,
} board_variant_t;

/** Local oscillator technologies and part numbers fitted at the CLK location. */
typedef enum {
  BOARD_CLOCK_DEVICE_NONE = 0, /**< No controllable local oscillator fitted. */
  BOARD_CLOCK_DEVICE_DCTCXO_SIT5156, /**< Register-controlled SiT5156 DCTCXO. */
  BOARD_CLOCK_DEVICE_DCO_SIT3907, /**< CLK_DP-controlled SiT3907 DCO. */
} board_clock_device_t;

/** FPGA technologies supported by this PCB definition. */
typedef enum {
  BOARD_FPGA_DEVICE_NONE = 0, /**< No FPGA populated. */
  BOARD_FPGA_DEVICE_ICE40UP5K, /**< Lattice iCE40UP5K populated. */
} board_fpga_device_t;

/**
 * UWB device capability relevant to antenna-path selection.
 *
 * This is deliberately a Board-level capability rather than the raw device
 * ID. The DW3000 driver still verifies the exact silicon ID at initialization.
 * A future single-path DW3000-family population can therefore share the Board
 * and workflow interfaces while rejecting RF2 and dual-path requests.
 */
typedef enum {
  BOARD_UWB_DEVICE_NONE = 0, /**< No UWB device populated. */
  BOARD_UWB_DEVICE_DW3000_SINGLE_RF, /**< DW3000-family device with RF1 only. */
  BOARD_UWB_DEVICE_DW3000_DUAL_RF, /**< DW3000-family device capable of RF1 and RF2. */
} board_uwb_device_t;

/** Bit mask of physical UWB RF paths. */
typedef enum {
  BOARD_RF_PATH_NONE = 0U,
  BOARD_RF_PATH_1 = 1U << 0,
  BOARD_RF_PATH_2 = 1U << 1,
  BOARD_RF_PATH_BOTH = BOARD_RF_PATH_1 | BOARD_RF_PATH_2,
} board_rf_path_mask_t;

/** Electrical drive state for the SiT3907 CLK_DP control line. */
typedef enum {
  BOARD_CLKDP_TRISTATE = 0,
  BOARD_CLKDP_DRIVE_LOW,
  BOARD_CLKDP_DRIVE_HIGH,
} board_clkdp_mode_t;

/** Physical UWB population: selected device capability and routed RF paths. */
typedef struct {
  board_uwb_device_t device; /**< UWB silicon RF capability. */
  board_rf_path_mask_t rf_paths_fitted; /**< RF paths populated on this PCB. */
} board_uwb_description_t;

/** UWB resources required by a workflow's selected radio policy. */
typedef struct {
  board_rf_path_mask_t required_rf_paths; /**< Paths needed by a radio policy. */
} board_uwb_requirement_t;

/** Physical FPGA population. `NONE` means the MCU must not access FPGA pins. */
typedef struct {
  board_fpga_device_t device; /**< Fitted FPGA technology, if any. */
} board_fpga_description_t;

/**
 * Physical clock population and its optional reference-counter route.
 *
 * `reference_counter_connected` describes PCB routing, not clock detection. A
 * running clock is verified later by the clock service.
 */
typedef struct {
  board_clock_device_t device; /**< Fitted controllable oscillator. */
  bool reference_counter_connected; /**< CLK output routed to the reference counter. */
} board_clock_description_t;

/**
 * Complete immutable description of one physical BOM population.
 *
 * Component descriptions state what is fitted and connected. A Board variant
 * selects one of these descriptions; workflows state only their requirements
 * and are checked against it during application startup.
 */
typedef struct {
  const char *name; /**< Human-readable immutable population name. */
  board_uwb_description_t uwb; /**< UWB device and RF population. */
  board_fpga_description_t fpga; /**< FPGA population. */
  board_clock_description_t clock; /**< Local-clock population. */
} board_description_t;

/**
 * @brief Load one validated physical PCB population and initialize its safe
 *        idle pin states.
 * @param variant Compile-time-selected entry from `board_variant_t`.
 * @return `BOARD_OK`, or `BOARD_BAD_ARG` when the variant is invalid.
 */
board_status_t board_init(board_variant_t variant);

/** @brief Return the immutable description selected by the last `board_init()`. */
const board_description_t *board_get_description(void);

/** @brief Return a stable human-readable name for a Board status value. */
const char *board_status_name(board_status_t status);

/** @brief Return a stable human-readable name for a clock device selection. */
const char *board_clock_device_name(board_clock_device_t device);

/** @brief Return a stable human-readable name for an FPGA device selection. */
const char *board_fpga_device_name(board_fpga_device_t device);

/** @brief Return a stable human-readable name for a UWB device capability. */
const char *board_uwb_device_name(board_uwb_device_t device);

/**
 * @brief Verify that the selected UWB population can satisfy a workflow.
 *
 * The check covers both UWB-device capability and physically fitted RF paths.
 * It rejects, for example, dual-RF PDoA on a single-RF device or an RF2 policy
 * on a board where only RF1 is populated.
 */
board_status_t board_validate_uwb_requirement(
    const board_uwb_requirement_t *requirement);

/** @brief Return the Board-to-DW3000 transport callbacks for the fitted UWB device. */
const struct dw3000_platform *board_uwb_get_platform(void);

/** @brief Return FPGA configuration callbacks, or `NULL` when no FPGA is fitted. */
const struct ice40up5k_platform *board_fpga_get_platform(void);

/**
 * @brief Control the current RTL's FPGA_EN input.
 *
 * This does not control FPGA power or configuration; it only gates user logic
 * after a successful configuration.
 */
board_status_t board_fpga_set_user_enabled(bool enabled);

/** @brief Return the HAL millisecond tick used for service scheduling. */
uint32_t board_get_time_ms(void);

/**
 * @brief Read the reference counter driven by the fitted local clock.
 * @param timestamp_ms Output reference-time counter value.
 * @return `true` only while this variant has started the reference counter.
 */
bool board_get_reference_time_ms(uint32_t *timestamp_ms);

/** @brief Delay for at least the requested number of milliseconds. */
void board_delay_ms(uint32_t delay_ms);

/** @brief Busy-wait for a short delay in microseconds. */
void board_delay_us(uint32_t delay_us);

/**
 * @brief Write a register through the fitted register-controlled clock path.
 * @return `BOARD_UNSUPPORTED` unless the selected population fits the DCTCXO.
 */
board_status_t board_clock_register_write(
    uint8_t register_address,
    const uint8_t *data,
    size_t len);

/**
 * @brief Read a register through the fitted register-controlled clock path.
 * @return `BOARD_UNSUPPORTED` unless the selected population fits the DCTCXO.
 */
board_status_t board_clock_register_read(
    uint8_t register_address,
    uint8_t *data,
    size_t len);

/** @brief Set the SiT3907 CLK_DP pin to high, low, or high-impedance. */
void board_clkdp_set_mode(board_clkdp_mode_t mode);

/** @brief Return the raw clock reference counter, or zero when stopped. */
uint32_t board_reference_counter_get(void);

/**
 * @brief Queue bytes for non-blocking PC serial transmission.
 * @return `BOARD_BUSY` if the transmit queue has insufficient capacity.
 */
board_status_t board_pc_transmit(const uint8_t *data, size_t len);

/**
 * @brief Send a short PC serial message synchronously when the queue is idle.
 * @return `BOARD_BUSY` if queued DMA traffic is in progress.
 */
board_status_t board_pc_transmit_blocking(
    const uint8_t *data,
    size_t len);

/** @brief Abort queued and active PC serial transmission. */
void board_pc_tx_abort(void);

/** @brief Service transmit timeout recovery and start pending queue data. */
void board_pc_tx_process(void);

/** @brief Return free bytes in the PC serial transmit queue. */
size_t board_pc_tx_available(void);

/** @brief Return the number of PC serial transmit errors since `board_init()`. */
uint32_t board_pc_tx_error_count(void);

/**
 * @brief Calculate the STM32 hardware CRC32 used by the serial protocols.
 * @param data Non-empty input byte sequence.
 * @param len Number of input bytes.
 * @param crc Output CRC32 value.
 */
board_status_t board_crc32_calculate(
    const uint8_t *data,
    size_t len,
    uint32_t *crc);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_BOARD_H */
