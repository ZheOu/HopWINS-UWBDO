/**
  ******************************************************************************
  * @file           : fpga_service.h
  * @brief          : FPGA configuration and state service
  ******************************************************************************
  */

#ifndef HOPWINS_FPGA_SERVICE_H
#define HOPWINS_FPGA_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "fpga_image.h"

typedef struct {
  fpga_image_info_t image;
  ice40_device_t device;
  ice40_status_t configure_status;
  bool cdone_pin;
} fpga_service_state_t;

ice40_status_t fpga_service_init(const ice40_platform_t *platform);
ice40_status_t fpga_service_configure(void);
const fpga_service_state_t *fpga_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_FPGA_SERVICE_H */
