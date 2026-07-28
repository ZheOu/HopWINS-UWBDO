/**
  ******************************************************************************
  * @file           : fpga_image.h
  * @brief          : Access to the FPGA image embedded by the linker
  ******************************************************************************
  */

#ifndef HOPWINS_FPGA_IMAGE_H
#define HOPWINS_FPGA_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ice40.h"

typedef struct {
  ice40_image_t image;
  ice40_status_t status;
  uint32_t sync_offset;
  uint32_t capacity;
} fpga_image_info_t;

ice40_status_t fpga_image_load_embedded(fpga_image_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_FPGA_IMAGE_H */
