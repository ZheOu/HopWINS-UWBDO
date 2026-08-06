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

typedef enum {
  BOARD_OK = 0,
  BOARD_ERROR = -1,
  BOARD_TIMEOUT = -2,
  BOARD_BAD_ARG = -3,
  BOARD_BUSY = -4,
} board_status_t;

/* Each variant describes one real BOM population of this PCB. */
typedef enum {
  BOARD_VARIANT_UWB_RF1 = 0,
  BOARD_VARIANT_UWB_RF1_SIT5156,
  BOARD_VARIANT_FULL_SIT5156,
  BOARD_VARIANT_FULL_SIT3907,
  BOARD_VARIANT_COUNT,
} board_variant_t;

typedef enum {
  BOARD_CLOCK_DEVICE_NONE = 0,
  BOARD_CLOCK_DEVICE_SIT5156,
  BOARD_CLOCK_DEVICE_SIT3907,
} board_clock_device_t;

typedef enum {
  BOARD_RF_PATH_NONE = 0U,
  BOARD_RF_PATH_1 = 1U << 0,
  BOARD_RF_PATH_2 = 1U << 1,
  BOARD_RF_PATH_BOTH = BOARD_RF_PATH_1 | BOARD_RF_PATH_2,
} board_rf_path_mask_t;

typedef enum {
  BOARD_CLKDP_TRISTATE = 0,
  BOARD_CLKDP_DRIVE_LOW,
  BOARD_CLKDP_DRIVE_HIGH,
} board_clkdp_mode_t;

typedef struct {
  const char *name;
  board_clock_device_t clock_device;
  board_rf_path_mask_t available_rf_paths;
  bool fpga_fitted;
  bool external_clock_counter_connected;
} board_description_t;

board_status_t board_init(board_variant_t variant);
const board_description_t *board_get_description(void);

const struct dw3000_platform *board_uwb_get_platform(void);
const struct ice40up5k_platform *board_fpga_get_platform(void);

/* Controls the current RTL's FPGA_EN input, not FPGA power or configuration. */
board_status_t board_fpga_set_user_enabled(bool enabled);

uint32_t board_get_time_ms(void);
bool board_get_reference_time_ms(uint32_t *timestamp_ms);
void board_delay_ms(uint32_t delay_ms);
void board_delay_us(uint32_t delay_us);

board_status_t board_clock_i2c_write(
    uint8_t register_address,
    const uint8_t *data,
    size_t len);
board_status_t board_clock_i2c_read(
    uint8_t register_address,
    uint8_t *data,
    size_t len);
void board_clkdp_set_mode(board_clkdp_mode_t mode);
uint32_t board_external_clock_counter_get(void);

board_status_t board_pc_transmit(const uint8_t *data, size_t len);
board_status_t board_pc_transmit_blocking(
    const uint8_t *data,
    size_t len);
void board_pc_tx_abort(void);
void board_pc_tx_process(void);
size_t board_pc_tx_available(void);
uint32_t board_pc_tx_error_count(void);

board_status_t board_crc32_calculate(
    const uint8_t *data,
    size_t len,
    uint32_t *crc);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_BOARD_H */
