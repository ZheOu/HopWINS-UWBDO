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

#include "ice40up5k.h"

/** Validated view of the linker-embedded image. */
typedef struct {
  ice40up5k_image_t image;
  uint32_t sync_offset;
} fpga_image_info_t;

/** Resolve linker symbols and validate the embedded image without copying it. */
ice40up5k_status_t fpga_image_load_embedded(fpga_image_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_FPGA_IMAGE_H */
