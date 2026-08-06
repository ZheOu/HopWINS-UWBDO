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

/** Retained startup state for Console reporting and later diagnostics. */
typedef struct {
  fpga_image_info_t image;
  ice40up5k_result_t result;
  ice40up5k_status_t image_status;
  ice40up5k_status_t configure_status;
  uint32_t attempt_count;
  bool cdone_pin;
} fpga_service_state_t;

/** Bind the platform and validate the linker-embedded image. */
ice40up5k_status_t fpga_service_init(
    const ice40up5k_platform_t *platform);

/** Configure the FPGA, retrying only transient transport/CDONE failures. */
ice40up5k_status_t fpga_service_configure(void);

const fpga_service_state_t *fpga_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_FPGA_SERVICE_H */
