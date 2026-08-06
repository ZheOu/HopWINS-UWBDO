/**
  ******************************************************************************
  * @file           : fpga_image.c
  * @brief          : Access to the FPGA image embedded by the linker
  ******************************************************************************
  */

#include "fpga_image.h"

#include <stdint.h>

extern const uint8_t __fpga_image_start__[];
extern const uint8_t __fpga_image_end__[];
extern const uint8_t __fpga_image_capacity__[];

ice40up5k_status_t fpga_image_load_embedded(fpga_image_info_t *info)
{
  uintptr_t start;
  uintptr_t end;
  uintptr_t capacity;

  if (info == NULL) {
    return ICE40UP5K_STATUS_BAD_ARG;
  }

  *info = (fpga_image_info_t){0};
  start = (uintptr_t)__fpga_image_start__;
  end = (uintptr_t)__fpga_image_end__;
  capacity = (uintptr_t)__fpga_image_capacity__;

  if ((end < start) ||
      ((end - start) > UINT32_MAX) ||
      (capacity > UINT32_MAX) ||
      ((end - start) > capacity)) {
    return ICE40UP5K_STATUS_BAD_IMAGE;
  }

  info->image.data = __fpga_image_start__;
  info->image.length = (uint32_t)(end - start);
  return ice40up5k_validate_image(&info->image, &info->sync_offset);
}
