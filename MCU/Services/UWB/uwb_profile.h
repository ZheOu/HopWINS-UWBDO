/**
  ******************************************************************************
  * @file           : uwb_profile.h
  * @brief          : Application UWB radio and network profiles
  ******************************************************************************
  */

#ifndef HOPWINS_UWB_PROFILE_H
#define HOPWINS_UWB_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dw3000.h"

#include <stdint.h>

typedef struct {
  dw3000_radio_config_t radio;
  uint16_t pan_id;
  uint16_t destination_address;
  uint16_t source_address;
} uwb_profile_t;

extern const uwb_profile_t g_uwb_default_profile;

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_UWB_PROFILE_H */
