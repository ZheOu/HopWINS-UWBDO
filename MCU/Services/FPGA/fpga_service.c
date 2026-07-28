/**
  ******************************************************************************
  * @file           : fpga_service.c
  * @brief          : FPGA configuration and state service
  ******************************************************************************
  */

#include "fpga_service.h"

#define FPGA_SERVICE_MAX_ATTEMPTS 3U

static const ice40_platform_t *s_platform;
static fpga_service_state_t s_state;

ice40_status_t fpga_service_init(const ice40_platform_t *platform)
{
  s_platform = platform;
  s_state = (fpga_service_state_t){
    .configure_status = ICE40_STATUS_BAD_ARG,
  };

  if (s_platform == NULL) {
    s_state.image.status = ICE40_STATUS_BAD_ARG;
    return s_state.image.status;
  }

  return fpga_image_load_embedded(&s_state.image);
}

ice40_status_t fpga_service_configure(void)
{
  if ((s_platform == NULL) || (s_state.image.status != ICE40_STATUS_OK)) {
    s_state.configure_status = (s_platform == NULL)
        ? ICE40_STATUS_BAD_ARG
        : s_state.image.status;
    return s_state.configure_status;
  }

  s_state.configure_status = ice40_configure_retry(
      &s_state.device,
      s_platform,
      &s_state.image.image,
      FPGA_SERVICE_MAX_ATTEMPTS);
  if (s_platform->get_cdone != NULL) {
    s_state.cdone_pin = s_platform->get_cdone();
  }
  return s_state.configure_status;
}

const fpga_service_state_t *fpga_service_get_state(void)
{
  return &s_state;
}
