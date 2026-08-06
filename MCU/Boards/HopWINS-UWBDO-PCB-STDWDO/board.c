/**
  ******************************************************************************
  * @file           : board.c
  * @brief          : Board definition file
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "board.h"
#include "dw3000.h"
#include "ice40up5k.h"
#include "main.h"
#include "sit5156.h"

#include <string.h>

#define BOARD_PC_TX_BUFFER_SIZE UINT32_C(16384)
#define BOARD_PC_TX_DMA_TIMEOUT_MS UINT32_C(100)
#define BOARD_FPGA_SPI_CHUNK_BYTES ((size_t)1024)

/* Peripheral Handles --------------------------------------------------------*/
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c2;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim2;
extern CRC_HandleTypeDef hcrc;

typedef enum {
  BOARD_SPI_TARGET_UWB = 0,
  BOARD_SPI_TARGET_FPGA,
} board_spi_target_t;

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
  board_gpio_t user_enable;
  board_gpio_t reset_n;
  board_gpio_t done;
} board_fpga_t;

typedef struct {
  board_i2c_device_t i2c;
  board_gpio_t clkdp;
} board_clock_t;

typedef struct {
  board_uwb_t uwb;
  board_fpga_t fpga;
  board_clock_t clock;
  board_uart_device_t pc_uart;
  board_timer_device_t external_clock_timer;
} board_components_t;

static int32_t board_uwb_hardware_reset(void);
static int32_t board_uwb_spi_set_slow_rate(void);
static int32_t board_uwb_spi_set_fast_rate(void);
static int32_t board_uwb_spi_read(
    const uint8_t *header,
    uint16_t header_len,
    uint8_t *data,
    uint16_t data_len);
static int32_t board_uwb_spi_write(
    const uint8_t *header,
    uint16_t header_len,
    const uint8_t *data,
    uint16_t data_len,
    const uint8_t *trailer,
    uint16_t trailer_len);
static void board_fpga_set_creset_b(bool high);
static bool board_fpga_get_cdone(void);
static bool board_fpga_spi_select(void);
static void board_fpga_spi_deselect(void);
static bool board_fpga_spi_write(const uint8_t *data, size_t len);
static void board_gpio_write(const board_gpio_t *gpio, bool active);
static bool board_gpio_read(const board_gpio_t *gpio);
static void board_spi_deselect_all(void);
static board_status_t board_spi_select(board_spi_target_t target);
static void board_spi_deselect(board_spi_target_t target);
static board_status_t board_spi_receive_after_header(
    board_spi_target_t target,
    const uint8_t *header,
    size_t header_len,
    uint8_t *rx,
    size_t rx_len);
static board_status_t board_i2c_mem_write_7bit(
    const board_i2c_device_t *device,
    uint16_t mem_addr,
    uint16_t mem_addr_size,
    const uint8_t *data,
    size_t len);
static board_status_t board_i2c_mem_read_7bit(
    const board_i2c_device_t *device,
    uint16_t mem_addr,
    uint16_t mem_addr_size,
    uint8_t *data,
    size_t len);
static board_status_t board_external_clock_counter_start(void);
static board_status_t board_pc_tx_start(void);
static uint32_t board_pc_tx_used(uint32_t head, uint32_t tail);
static bool board_pc_tx_busy(void);

static uint8_t s_pc_tx_buffer[BOARD_PC_TX_BUFFER_SIZE]
    __attribute__((aligned(32)));
static volatile uint32_t s_pc_tx_head;
static volatile uint32_t s_pc_tx_tail;
static volatile uint32_t s_pc_tx_in_flight;
static volatile uint32_t s_pc_tx_started_ms;
static volatile uint32_t s_pc_tx_errors;
static volatile bool s_pc_tx_dma_active;
static bool s_external_clock_counter_running;

static const board_description_t s_variants[BOARD_VARIANT_COUNT] = {
  [BOARD_VARIANT_UWB_RF1] = {
    .name = "UWB-RF1",
    .clock_device = BOARD_CLOCK_DEVICE_NONE,
    .available_rf_paths = BOARD_RF_PATH_1,
    .fpga_fitted = false,
    .external_clock_counter_connected = false,
  },
  [BOARD_VARIANT_UWB_RF1_SIT5156] = {
    .name = "UWB-RF1-SiT5156",
    .clock_device = BOARD_CLOCK_DEVICE_SIT5156,
    .available_rf_paths = BOARD_RF_PATH_1,
    .fpga_fitted = false,
    .external_clock_counter_connected = true,
  },
  [BOARD_VARIANT_FULL_SIT5156] = {
    .name = "Full-SiT5156",
    .clock_device = BOARD_CLOCK_DEVICE_SIT5156,
    .available_rf_paths = BOARD_RF_PATH_BOTH,
    .fpga_fitted = true,
    .external_clock_counter_connected = true,
  },
  [BOARD_VARIANT_FULL_SIT3907] = {
    .name = "Full-SiT3907",
    .clock_device = BOARD_CLOCK_DEVICE_SIT3907,
    .available_rf_paths = BOARD_RF_PATH_BOTH,
    .fpga_fitted = true,
    .external_clock_counter_connected = true,
  },
};

static board_description_t s_description = {
  .name = "Unconfigured",
  .clock_device = BOARD_CLOCK_DEVICE_NONE,
  .available_rf_paths = BOARD_RF_PATH_NONE,
};

static board_components_t s_board = {
  .uwb = {
    .spi = {
      .hspi = &hspi1,
      .cs = {SPI1_CSUWB_GPIO_Port, SPI1_CSUWB_Pin, GPIO_PIN_RESET},
      .timeout_ms = 10U,
    },
    .reset_n = {GPIO_UWBRST_GPIO_Port, GPIO_UWBRST_Pin, GPIO_PIN_SET},
    .irq = {GPIO_UWBIRQ_GPIO_Port, GPIO_UWBIRQ_Pin, GPIO_PIN_SET},
  },
  .fpga = {
    .spi = {
      .hspi = &hspi1,
      .cs = {SPI1_CSFPGA_GPIO_Port, SPI1_CSFPGA_Pin, GPIO_PIN_RESET},
      .timeout_ms = 10U,
    },
    .user_enable = {GPIO_FPGAEN_GPIO_Port, GPIO_FPGAEN_Pin, GPIO_PIN_SET},
    .reset_n = {GPIO_FPGARST_GPIO_Port, GPIO_FPGARST_Pin, GPIO_PIN_SET},
    .done = {GPIO_FPGADONE_GPIO_Port, GPIO_FPGADONE_Pin, GPIO_PIN_SET},
  },
  .clock = {
    .i2c = {
      .hi2c = &hi2c2,
      .address_7bit = SIT5156_I2C_ADDRESS_7BIT,
      .timeout_ms = 10U,
    },
    .clkdp = {GPIO_CLKDP_GPIO_Port, GPIO_CLKDP_Pin, GPIO_PIN_SET},
  },
  .pc_uart = {
    .huart = &huart1,
    .timeout_ms = 10U,
  },
  .external_clock_timer = {
    .htim = &htim2,
  },
};

static const dw3000_platform_t s_dw3000_platform = {
  .hardware_reset = board_uwb_hardware_reset,
  .spi_set_slow_rate = board_uwb_spi_set_slow_rate,
  .spi_set_fast_rate = board_uwb_spi_set_fast_rate,
  .spi_read = board_uwb_spi_read,
  .spi_write = board_uwb_spi_write,
  .delay_ms = board_delay_ms,
  .delay_us = board_delay_us,
  .lock = NULL,
  .unlock = NULL,
};

static const ice40up5k_platform_t s_ice40up5k_platform = {
  .set_creset_b = board_fpga_set_creset_b,
  .read_cdone = board_fpga_get_cdone,
  .spi_select = board_fpga_spi_select,
  .spi_deselect = board_fpga_spi_deselect,
  .spi_write = board_fpga_spi_write,
  .delay_us = board_delay_us,
};

static board_status_t hal_to_board_status(HAL_StatusTypeDef status)
{
  switch (status) {
    case HAL_OK:
      return BOARD_OK;
    case HAL_BUSY:
      return BOARD_BUSY;
    case HAL_TIMEOUT:
      return BOARD_TIMEOUT;
    default:
      return BOARD_ERROR;
  }
}

static GPIO_PinState inactive_state(GPIO_PinState active_state)
{
  return (active_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

static const board_spi_device_t *spi_device_from_target(board_spi_target_t target)
{
  switch (target) {
    case BOARD_SPI_TARGET_UWB:
      return &s_board.uwb.spi;
    case BOARD_SPI_TARGET_FPGA:
      return s_description.fpga_fitted ? &s_board.fpga.spi : NULL;
    default:
      return NULL;
  }
}

static uint16_t checked_len(size_t len)
{
  return (len <= UINT16_MAX) ? (uint16_t)len : 0U;
}

static void board_uwb_reset_release(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = s_board.uwb.reset_n.pin;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(s_board.uwb.reset_n.port, &gpio);

  /* RSTn must only be pulled low or released; it must never be driven high. */
  HAL_GPIO_WritePin(
      s_board.uwb.reset_n.port,
      s_board.uwb.reset_n.pin,
      GPIO_PIN_SET);
}

board_status_t board_init(board_variant_t variant)
{
  if ((uint32_t)variant >= (uint32_t)BOARD_VARIANT_COUNT) {
    return BOARD_BAD_ARG;
  }

  s_description = s_variants[variant];
  s_pc_tx_head = 0U;
  s_pc_tx_tail = 0U;
  s_pc_tx_in_flight = 0U;
  s_pc_tx_started_ms = 0U;
  s_pc_tx_errors = 0U;
  s_pc_tx_dma_active = false;
  s_external_clock_counter_running = false;

  board_spi_deselect_all();
  if (s_description.fpga_fitted) {
    board_gpio_write(&s_board.fpga.user_enable, false);
    board_gpio_write(&s_board.fpga.reset_n, false);
  }
  board_uwb_reset_release();
  if (s_description.clock_device == BOARD_CLOCK_DEVICE_SIT3907) {
    board_clkdp_set_mode(BOARD_CLKDP_TRISTATE);
  }
  if (s_description.external_clock_counter_connected &&
      (board_external_clock_counter_start() != BOARD_OK)) {
    return BOARD_ERROR;
  }
  return BOARD_OK;
}

uint32_t board_get_time_ms(void)
{
  return HAL_GetTick();
}

const struct dw3000_platform *board_uwb_get_platform(void)
{
  return &s_dw3000_platform;
}

const struct ice40up5k_platform *board_fpga_get_platform(void)
{
  return s_description.fpga_fitted ? &s_ice40up5k_platform : NULL;
}

board_status_t board_fpga_set_user_enabled(bool enabled)
{
  if (!s_description.fpga_fitted) {
    return BOARD_ERROR;
  }

  board_gpio_write(&s_board.fpga.user_enable, enabled);
  return BOARD_OK;
}

const board_description_t *board_get_description(void)
{
  return &s_description;
}

static void board_gpio_write(const board_gpio_t *gpio, bool active)
{
  if ((gpio == NULL) || (gpio->port == NULL)) {
    return;
  }

  HAL_GPIO_WritePin(gpio->port, gpio->pin, active ? gpio->active_state : inactive_state(gpio->active_state));
}

static bool board_gpio_read(const board_gpio_t *gpio)
{
  if ((gpio == NULL) || (gpio->port == NULL)) {
    return false;
  }

  return HAL_GPIO_ReadPin(gpio->port, gpio->pin) == gpio->active_state;
}

static void board_spi_deselect_all(void)
{
  board_gpio_write(&s_board.uwb.spi.cs, false);
  if (s_description.fpga_fitted) {
    board_gpio_write(&s_board.fpga.spi.cs, false);
  }
}

static board_status_t board_spi_select(board_spi_target_t target)
{
  const board_spi_device_t *dev = spi_device_from_target(target);

  if (dev == NULL) {
    return BOARD_BAD_ARG;
  }

  board_spi_deselect_all();
  board_gpio_write(&dev->cs, true);
  return BOARD_OK;
}

static void board_spi_deselect(board_spi_target_t target)
{
  const board_spi_device_t *dev = spi_device_from_target(target);

  if (dev == NULL) {
    return;
  }

  board_gpio_write(&dev->cs, false);
}

static board_status_t board_spi_receive_after_header(
    board_spi_target_t target,
    const uint8_t *header,
    size_t header_len,
    uint8_t *rx,
    size_t rx_len)
{
  const board_spi_device_t *dev = spi_device_from_target(target);
  uint16_t tx_header_len = checked_len(header_len);
  uint16_t read_len = checked_len(rx_len);
  board_status_t result;

  if ((dev == NULL) || (header == NULL && header_len != 0U) || (rx == NULL && rx_len != 0U) ||
      (tx_header_len == 0U && header_len != 0U) || (read_len == 0U && rx_len != 0U)) {
    return BOARD_BAD_ARG;
  }

  if ((header_len == 0U) && (rx_len == 0U)) {
    return BOARD_OK;
  }

  result = board_spi_select(target);
  if (result == BOARD_OK && tx_header_len > 0U) {
    result = hal_to_board_status(HAL_SPI_Transmit(dev->hspi, (uint8_t *)header, tx_header_len, dev->timeout_ms));
  }
  if (result == BOARD_OK && read_len > 0U) {
    result = hal_to_board_status(HAL_SPI_Receive(dev->hspi, rx, read_len, dev->timeout_ms));
  }
  board_spi_deselect(target);
  return result;
}

static board_status_t board_i2c_mem_write_7bit(
    const board_i2c_device_t *dev,
    uint16_t mem_addr,
    uint16_t mem_addr_size,
    const uint8_t *data,
    size_t len)
{
  uint16_t data_len = checked_len(len);

  if ((dev == NULL) || (dev->hi2c == NULL) || (data == NULL && len != 0U) || (data_len == 0U && len != 0U)) {
    return BOARD_BAD_ARG;
  }

  if (len == 0U) {
    return BOARD_OK;
  }

  return hal_to_board_status(HAL_I2C_Mem_Write(
      dev->hi2c,
      (uint16_t)(dev->address_7bit << 1U),
      mem_addr,
      mem_addr_size,
      (uint8_t *)data,
      data_len,
      dev->timeout_ms));
}

static board_status_t board_i2c_mem_read_7bit(
    const board_i2c_device_t *dev,
    uint16_t mem_addr,
    uint16_t mem_addr_size,
    uint8_t *data,
    size_t len)
{
  uint16_t data_len = checked_len(len);

  if ((dev == NULL) || (dev->hi2c == NULL) || (data == NULL && len != 0U) || (data_len == 0U && len != 0U)) {
    return BOARD_BAD_ARG;
  }

  if (len == 0U) {
    return BOARD_OK;
  }

  return hal_to_board_status(HAL_I2C_Mem_Read(
      dev->hi2c,
      (uint16_t)(dev->address_7bit << 1U),
      mem_addr,
      mem_addr_size,
      data,
      data_len,
      dev->timeout_ms));
}

board_status_t board_clock_i2c_write(
    uint8_t register_address,
    const uint8_t *data,
    size_t len)
{
  if (s_description.clock_device != BOARD_CLOCK_DEVICE_SIT5156) {
    return BOARD_ERROR;
  }

  return board_i2c_mem_write_7bit(
      &s_board.clock.i2c,
      register_address,
      I2C_MEMADD_SIZE_8BIT,
      data,
      len);
}

board_status_t board_clock_i2c_read(
    uint8_t register_address,
    uint8_t *data,
    size_t len)
{
  if (s_description.clock_device != BOARD_CLOCK_DEVICE_SIT5156) {
    return BOARD_ERROR;
  }

  return board_i2c_mem_read_7bit(
      &s_board.clock.i2c,
      register_address,
      I2C_MEMADD_SIZE_8BIT,
      data,
      len);
}

board_status_t board_pc_transmit(const uint8_t *data, size_t len)
{
  uint32_t head;
  uint32_t tail;
  uint32_t first_len;
  board_status_t start_status;

  if ((data == NULL && len != 0U) ||
      (len >= BOARD_PC_TX_BUFFER_SIZE)) {
    return BOARD_BAD_ARG;
  }

  if (len == 0U) {
    return BOARD_OK;
  }

  head = s_pc_tx_head;
  tail = s_pc_tx_tail;
  if (len > (BOARD_PC_TX_BUFFER_SIZE - board_pc_tx_used(head, tail) - 1U)) {
    return BOARD_BUSY;
  }

  first_len = BOARD_PC_TX_BUFFER_SIZE - head;
  if (first_len > len) {
    first_len = (uint32_t)len;
  }

  memcpy(&s_pc_tx_buffer[head], data, first_len);
  if (len > first_len) {
    memcpy(
        s_pc_tx_buffer,
        &data[first_len],
        len - first_len);
  }

  __DMB();
  s_pc_tx_head =
      (head + (uint32_t)len) % BOARD_PC_TX_BUFFER_SIZE;

  start_status = board_pc_tx_start();
  return (start_status == BOARD_BUSY) ? BOARD_OK : start_status;
}

board_status_t board_pc_transmit_blocking(
    const uint8_t *data,
    size_t len)
{
  uint16_t data_len = checked_len(len);

  if (((data == NULL) && (len != 0U)) ||
      ((data_len == 0U) && (len != 0U))) {
    return BOARD_BAD_ARG;
  }
  if (len == 0U) {
    return BOARD_OK;
  }
  if (board_pc_tx_busy()) {
    return BOARD_BUSY;
  }

  return hal_to_board_status(HAL_UART_Transmit(
      s_board.pc_uart.huart,
      data,
      data_len,
      s_board.pc_uart.timeout_ms));
}

void board_pc_tx_abort(void)
{
  (void)HAL_UART_AbortTransmit(s_board.pc_uart.huart);
  s_pc_tx_head = 0U;
  s_pc_tx_tail = 0U;
  s_pc_tx_in_flight = 0U;
  s_pc_tx_started_ms = 0U;
  s_pc_tx_dma_active = false;
}

void board_pc_tx_process(void)
{
  if (s_pc_tx_dma_active &&
      ((uint32_t)(board_get_time_ms() - s_pc_tx_started_ms) >=
       BOARD_PC_TX_DMA_TIMEOUT_MS)) {
    (void)HAL_UART_AbortTransmit(s_board.pc_uart.huart);
    s_pc_tx_in_flight = 0U;
    s_pc_tx_dma_active = false;
    s_pc_tx_errors++;
  }

  (void)board_pc_tx_start();
}

size_t board_pc_tx_available(void)
{
  uint32_t used = board_pc_tx_used(s_pc_tx_head, s_pc_tx_tail);
  return (size_t)(BOARD_PC_TX_BUFFER_SIZE - used - 1U);
}

static bool board_pc_tx_busy(void)
{
  return s_pc_tx_dma_active || (s_pc_tx_head != s_pc_tx_tail);
}

uint32_t board_pc_tx_error_count(void)
{
  return s_pc_tx_errors;
}

board_status_t board_crc32_calculate(
    const uint8_t *data,
    size_t len,
    uint32_t *crc)
{
  if ((data == NULL) || (len == 0U) || (len > UINT32_MAX) || (crc == NULL)) {
    return BOARD_BAD_ARG;
  }

  *crc = HAL_CRC_Calculate(
      &hcrc,
      (uint32_t *)(void *)data,
      (uint32_t)len) ^ UINT32_C(0xFFFFFFFF);
  return BOARD_OK;
}

static board_status_t board_external_clock_counter_start(void)
{
  board_status_t status;

  if (!s_description.external_clock_counter_connected) {
    return BOARD_ERROR;
  }

  __HAL_TIM_SET_COUNTER(s_board.external_clock_timer.htim, 0U);
  __HAL_TIM_CLEAR_FLAG(s_board.external_clock_timer.htim, TIM_FLAG_UPDATE);
  status = hal_to_board_status(
      HAL_TIM_Base_Start(s_board.external_clock_timer.htim));
  s_external_clock_counter_running = status == BOARD_OK;
  return status;
}

uint32_t board_external_clock_counter_get(void)
{
  return s_external_clock_counter_running
             ? __HAL_TIM_GET_COUNTER(s_board.external_clock_timer.htim)
             : 0U;
}

void board_clkdp_set_mode(board_clkdp_mode_t mode)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (s_description.clock_device != BOARD_CLOCK_DEVICE_SIT3907) {
    return;
  }

  GPIO_InitStruct.Pin = s_board.clock.clkdp.pin;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  switch (mode) {
    case BOARD_CLKDP_DRIVE_LOW:
      HAL_GPIO_WritePin(
          s_board.clock.clkdp.port,
          s_board.clock.clkdp.pin,
          GPIO_PIN_RESET);
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      break;
    case BOARD_CLKDP_DRIVE_HIGH:
      HAL_GPIO_WritePin(
          s_board.clock.clkdp.port,
          s_board.clock.clkdp.pin,
          GPIO_PIN_SET);
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      break;
    case BOARD_CLKDP_TRISTATE:
    default:
      GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
      break;
  }

  HAL_GPIO_Init(s_board.clock.clkdp.port, &GPIO_InitStruct);
}

static board_status_t board_spi_set_prescaler(
    board_spi_target_t target,
    uint32_t prescaler)
{
  const board_spi_device_t *dev = spi_device_from_target(target);

  if ((dev == NULL) || (dev->hspi == NULL)) {
    return BOARD_BAD_ARG;
  }

  if (HAL_SPI_GetState(dev->hspi) != HAL_SPI_STATE_READY) {
    return BOARD_ERROR;
  }

  if (dev->hspi->Init.BaudRatePrescaler == prescaler) {
    return BOARD_OK;
  }

  board_spi_deselect_all();
  dev->hspi->Init.BaudRatePrescaler = prescaler;
  return hal_to_board_status(HAL_SPI_Init(dev->hspi));
}

static board_status_t board_pc_tx_start(void)
{
  uint32_t head;
  uint32_t tail;
  uint32_t transfer_len;
  HAL_StatusTypeDef status;

  if (s_pc_tx_dma_active) {
    return BOARD_BUSY;
  }

  head = s_pc_tx_head;
  tail = s_pc_tx_tail;
  if (head == tail) {
    return BOARD_OK;
  }

  transfer_len =
      (head > tail) ? (head - tail) : (BOARD_PC_TX_BUFFER_SIZE - tail);
  if (transfer_len > UINT16_MAX) {
    transfer_len = UINT16_MAX;
  }

  s_pc_tx_in_flight = transfer_len;
  s_pc_tx_started_ms = board_get_time_ms();
  s_pc_tx_dma_active = true;
  status = HAL_UART_Transmit_DMA(
      s_board.pc_uart.huart,
      &s_pc_tx_buffer[tail],
      (uint16_t)transfer_len);
  if (status != HAL_OK) {
    s_pc_tx_in_flight = 0U;
    s_pc_tx_started_ms = 0U;
    s_pc_tx_dma_active = false;
    if (status != HAL_BUSY) {
      s_pc_tx_errors++;
    }
  }

  return hal_to_board_status(status);
}

static uint32_t board_pc_tx_used(uint32_t head, uint32_t tail)
{
  return (head >= tail)
             ? (head - tail)
             : (BOARD_PC_TX_BUFFER_SIZE - tail + head);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((huart == NULL) || (huart != s_board.pc_uart.huart)) {
    return;
  }

  s_pc_tx_tail =
      (s_pc_tx_tail + s_pc_tx_in_flight) % BOARD_PC_TX_BUFFER_SIZE;
  s_pc_tx_in_flight = 0U;
  s_pc_tx_started_ms = 0U;
  s_pc_tx_dma_active = false;
  (void)board_pc_tx_start();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((huart == NULL) || (huart != s_board.pc_uart.huart)) {
    return;
  }

  if (s_pc_tx_dma_active) {
    s_pc_tx_in_flight = 0U;
    s_pc_tx_started_ms = 0U;
    s_pc_tx_dma_active = false;
    s_pc_tx_errors++;
  }
}

static int32_t board_uwb_hardware_reset(void)
{
  GPIO_InitTypeDef gpio = {0};

  board_spi_deselect(BOARD_SPI_TARGET_UWB);

  HAL_GPIO_WritePin(
      s_board.uwb.reset_n.port,
      s_board.uwb.reset_n.pin,
      GPIO_PIN_RESET);
  gpio.Pin = s_board.uwb.reset_n.pin;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(s_board.uwb.reset_n.port, &gpio);

  board_delay_us(10U);
  board_uwb_reset_release();
  board_delay_ms(2U);
  return 0;
}

static int32_t board_uwb_spi_set_slow_rate(void)
{
  /*
   * SPI1 kernel clock is 48 MHz. DIV8 gives 6 MHz, below the DW3000
   * INIT_RC maximum of 7 MHz.
   */
  return (int32_t)board_spi_set_prescaler(
      BOARD_SPI_TARGET_UWB,
      SPI_BAUDRATEPRESCALER_8);
}

static int32_t board_uwb_spi_set_fast_rate(void)
{
  /* Keep the existing 12 MHz board rate after initialization. */
  return (int32_t)board_spi_set_prescaler(
      BOARD_SPI_TARGET_UWB,
      SPI_BAUDRATEPRESCALER_4);
}

static int32_t board_uwb_spi_read(
    const uint8_t *header,
    uint16_t header_len,
    uint8_t *data,
    uint16_t data_len)
{
  return (int32_t)board_spi_receive_after_header(
      BOARD_SPI_TARGET_UWB,
      header,
      header_len,
      data,
      data_len);
}

static int32_t board_uwb_spi_write(
    const uint8_t *header,
    uint16_t header_len,
    const uint8_t *data,
    uint16_t data_len,
    const uint8_t *trailer,
    uint16_t trailer_len)
{
  const board_spi_device_t *dev = &s_board.uwb.spi;
  board_status_t status;

  if (((header == NULL) && (header_len != 0U)) ||
      ((data == NULL) && (data_len != 0U)) ||
      ((trailer == NULL) && (trailer_len != 0U))) {
    return (int32_t)BOARD_BAD_ARG;
  }

  status = board_spi_select(BOARD_SPI_TARGET_UWB);
  if ((status == BOARD_OK) && (header_len != 0U)) {
    status = hal_to_board_status(HAL_SPI_Transmit(
        dev->hspi,
        (uint8_t *)header,
        header_len,
        dev->timeout_ms));
  }
  if ((status == BOARD_OK) && (data_len != 0U)) {
    status = hal_to_board_status(HAL_SPI_Transmit(
        dev->hspi,
        (uint8_t *)data,
        data_len,
        dev->timeout_ms));
  }
  if ((status == BOARD_OK) && (trailer_len != 0U)) {
    status = hal_to_board_status(HAL_SPI_Transmit(
        dev->hspi,
        (uint8_t *)trailer,
        trailer_len,
        dev->timeout_ms));
  }

  board_spi_deselect(BOARD_SPI_TARGET_UWB);
  return (int32_t)status;
}

/*
 * CRESET_B is active low. fpga.reset_n carries the raw pin level because its
 * active_state is GPIO_PIN_SET, so the level maps straight through: false
 * asserts reset, true releases it.
 */
static void board_fpga_set_creset_b(bool high)
{
  board_gpio_write(&s_board.fpga.reset_n, high);
}

static bool board_fpga_get_cdone(void)
{
  return board_gpio_read(&s_board.fpga.done);
}

/*
 * The DW3000 shares SPI1 and leaves its own baud rate behind, so the
 * configuration rate is forced here: MSIK 48 MHz / 4 = 12 MHz, inside the 1 MHz
 * to 25 MHz window iCE40 slave configuration requires. board_spi_set_prescaler()
 * returns early when the prescaler already matches, so the repeated selects
 * inside ice40up5k_configure() do not re-initialise SPI1 in the middle of the
 * sequence.
 */
static bool board_fpga_spi_select(void)
{
  if (board_spi_set_prescaler(
          BOARD_SPI_TARGET_FPGA,
          SPI_BAUDRATEPRESCALER_4) != BOARD_OK) {
    return false;
  }
  return board_spi_select(BOARD_SPI_TARGET_FPGA) == BOARD_OK;
}

static void board_fpga_spi_deselect(void)
{
  board_spi_deselect(BOARD_SPI_TARGET_FPGA);
}

/*
 * Deliberately leaves the chip select alone: the whole configuration image has
 * to be clocked out inside a single SPI_SS low window, and the dummy clocks
 * around it have to be clocked out with SPI_SS high. This callback therefore
 * writes through HAL without changing chip select between chunks.
 */
static bool board_fpga_spi_write(const uint8_t *data, size_t len)
{
  const board_spi_device_t *dev = &s_board.fpga.spi;
  size_t offset = 0U;

  if ((data == NULL) || (len == 0U)) {
    return false;
  }

  while (offset < len) {
    size_t remaining = len - offset;
    uint16_t chunk = (remaining > BOARD_FPGA_SPI_CHUNK_BYTES)
        ? (uint16_t)BOARD_FPGA_SPI_CHUNK_BYTES
        : (uint16_t)remaining;

    if (HAL_SPI_Transmit(
            dev->hspi,
            &data[offset],
            chunk,
            dev->timeout_ms) != HAL_OK) {
      return false;
    }
    offset += chunk;
  }
  return true;
}

void board_delay_ms(uint32_t delay_ms)
{
  HAL_Delay(delay_ms);
}

void board_delay_us(uint32_t delay_us)
{
  uint32_t ticks_per_ms;
  uint32_t ticks_remaining;
  uint32_t previous;

  while (delay_us >= 1000U) {
    HAL_Delay(1U);
    delay_us -= 1000U;
  }

  if (delay_us == 0U) {
    return;
  }

  ticks_per_ms = SysTick->LOAD + 1U;
  ticks_remaining = (uint32_t)(
      (((uint64_t)ticks_per_ms * delay_us) + 999U) / 1000U);
  previous = SysTick->VAL;

  while (ticks_remaining != 0U) {
    uint32_t current = SysTick->VAL;
    uint32_t elapsed = (previous >= current)
                           ? (previous - current)
                           : (previous + ticks_per_ms - current);

    if (elapsed >= ticks_remaining) {
      break;
    }

    ticks_remaining -= elapsed;
    previous = current;
  }
}

bool board_get_reference_time_ms(uint32_t *timestamp_ms)
{
  if ((timestamp_ms == NULL) || !s_external_clock_counter_running) {
    return false;
  }

  *timestamp_ms = board_external_clock_counter_get();
  return true;
}
