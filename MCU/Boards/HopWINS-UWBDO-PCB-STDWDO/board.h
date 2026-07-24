/**
  ******************************************************************************
  * @file           : board.h
  * @brief          : Board definition file
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef HOPWINS_BOARD_H
#define HOPWINS_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct dw3000_platform;

typedef enum {
  BOARD_OK = 0,
  BOARD_ERROR = -1,
  BOARD_TIMEOUT = -2,
  BOARD_BAD_ARG = -3,
} board_status_t;

typedef enum {
  BOARD_SPI_TARGET_UWB = 0,
  BOARD_SPI_TARGET_FPGA,
} board_spi_target_t;

typedef enum {
  BOARD_CLKDP_TRISTATE = 0,
  BOARD_CLKDP_DRIVE_LOW,
  BOARD_CLKDP_DRIVE_HIGH,
  BOARD_CLKDP_INPUT,
} board_clkdp_mode_t;

typedef enum {
  BOARD_CLOCK_XO_I2C = 0,
  BOARD_CLOCK_XO_CLKDP,
} board_clock_xo_t;

typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
  GPIO_PinState active_state;
} board_gpio_t;

typedef struct {
  SPI_HandleTypeDef *hspi;
  board_gpio_t cs;
  uint32_t timeout_ms;
} board_spi_device_t;

typedef struct {
  I2C_HandleTypeDef *hi2c;
  uint16_t address_7bit;
  uint32_t timeout_ms;
} board_i2c_device_t;

typedef struct {
  UART_HandleTypeDef *huart;
  uint32_t timeout_ms;
} board_uart_device_t;

typedef struct {
  TIM_HandleTypeDef *htim;
} board_timer_device_t;

typedef struct {
  board_spi_device_t spi;
  board_gpio_t reset_n;
  board_gpio_t irq;
} board_uwb_t;

typedef struct {
  board_spi_device_t spi;
  board_gpio_t enable;
  board_gpio_t reset_n;
  board_gpio_t done;
} board_fpga_t;

typedef struct {
  board_i2c_device_t i2c;
} board_i2c_xo_t;

typedef struct {
  board_gpio_t dp;
} board_clkdp_xo_t;

typedef struct {
  board_clock_xo_t installed_xo;
  board_i2c_xo_t i2c_xo;
  board_clkdp_xo_t clkdp_xo;
} board_clock_t;

typedef struct {
  board_uwb_t uwb;
  board_fpga_t fpga;
  board_clock_t clock;
  board_uart_device_t pc_uart;
  board_timer_device_t external_clock_timer;
} board_components_t;

void board_init(void);
const board_components_t *board_get_components(void);
const struct dw3000_platform *board_uwb_get_platform(void);
void board_clock_select_xo(board_clock_xo_t installed_xo);
board_clock_xo_t board_clock_get_selected_xo(void);
const board_i2c_device_t *board_clock_get_i2c_xo(void);
void board_clock_i2c_xo_set_address(uint16_t address_7bit);

static inline void board_tcxo_set_i2c_address(uint16_t address_7bit)
{
  board_clock_i2c_xo_set_address(address_7bit);
}

void board_gpio_write(const board_gpio_t *gpio, bool active);
bool board_gpio_read(const board_gpio_t *gpio);

void board_spi_deselect_all(void);
board_status_t board_spi_select(board_spi_target_t target);
void board_spi_deselect(board_spi_target_t target);
board_status_t board_spi_transmit(board_spi_target_t target, const uint8_t *tx, size_t len);
board_status_t board_spi_receive_after_header(
    board_spi_target_t target,
    const uint8_t *header,
    size_t header_len,
    uint8_t *rx,
    size_t rx_len);
board_status_t board_spi_transmit_receive(
    board_spi_target_t target,
    const uint8_t *tx,
    uint8_t *rx,
    size_t len);

board_status_t board_i2c_mem_write_7bit(
    const board_i2c_device_t *dev,
    uint16_t mem_addr,
    uint16_t mem_addr_size,
    const uint8_t *data,
    size_t len);
board_status_t board_i2c_mem_read_7bit(
    const board_i2c_device_t *dev,
    uint16_t mem_addr,
    uint16_t mem_addr_size,
    uint8_t *data,
    size_t len);

board_status_t board_pc_transmit(const uint8_t *data, size_t len);
board_status_t board_pc_receive(uint8_t *data, size_t len);

board_status_t board_external_clock_counter_start(void);
board_status_t board_external_clock_counter_stop(void);
uint32_t board_external_clock_counter_get(void);
void board_external_clock_counter_reset(void);

void board_clkdp_set_mode(board_clkdp_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_BOARD_H */
