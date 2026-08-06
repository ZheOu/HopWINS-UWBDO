/**
  ******************************************************************************
  * @file           : fpga_service.c
  * @brief          : FPGA configuration and state service
  ******************************************************************************
  */

#include "fpga_service.h"

#define FPGA_SERVICE_MAX_ATTEMPTS   UINT32_C(3)
#define FPGA_SERVICE_RETRY_DELAY_US UINT32_C(1000)

static const ice40up5k_platform_t *s_platform;
static fpga_service_state_t s_state;

static bool status_is_retryable(ice40up5k_status_t status);

ice40up5k_status_t fpga_service_init(
    const ice40up5k_platform_t *platform)
{
  s_platform = platform;
  s_state = (fpga_service_state_t){
    .image_status = ICE40UP5K_STATUS_BAD_ARG,
    .configure_status = ICE40UP5K_STATUS_BAD_ARG,
  };

  if (s_platform == NULL) {
    return s_state.image_status;
  }

  s_state.image_status = fpga_image_load_embedded(&s_state.image);
  return s_state.image_status;
}

ice40up5k_status_t fpga_service_configure(void)
{
  if ((s_platform == NULL) ||
      (s_state.image_status != ICE40UP5K_STATUS_OK)) {
    s_state.configure_status = (s_platform == NULL)
        ? ICE40UP5K_STATUS_BAD_ARG
        : s_state.image_status;
    return s_state.configure_status;
  }

  s_state.attempt_count = 0U;
  for (uint32_t attempt = 0U;
       attempt < FPGA_SERVICE_MAX_ATTEMPTS;
       attempt++) {
    s_state.configure_status = ice40up5k_configure(
        &s_state.result,
        s_platform,
        &s_state.image.image);
    s_state.attempt_count = attempt + 1U;

    if ((s_state.configure_status == ICE40UP5K_STATUS_OK) ||
        !status_is_retryable(s_state.configure_status)) {
      break;
    }
    if ((attempt + 1U) < FPGA_SERVICE_MAX_ATTEMPTS) {
      s_platform->delay_us(FPGA_SERVICE_RETRY_DELAY_US);
    }
  }

  if (s_platform->read_cdone != NULL) {
    s_state.cdone_pin = s_platform->read_cdone();
  }
  return s_state.configure_status;
}

const fpga_service_state_t *fpga_service_get_state(void)
{
  return &s_state;
}

static bool status_is_retryable(ice40up5k_status_t status)
{
  return (status == ICE40UP5K_STATUS_SPI_ERROR) ||
         (status == ICE40UP5K_STATUS_CDONE_TIMEOUT) ||
         (status == ICE40UP5K_STATUS_CDONE_DROPPED);
}
