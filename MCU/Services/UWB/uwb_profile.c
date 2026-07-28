/**
  ******************************************************************************
  * @file           : uwb_profile.c
  * @brief          : Application UWB radio and network profiles
  ******************************************************************************
  */

#include "uwb_profile.h"

/*
 * Bring-up profile. TX power and antenna delays must be replaced with values
 * calibrated for the final PCB and applicable regional limits.
 */
const uwb_profile_t g_uwb_default_profile = {
  .radio = {
    .channel = DW3000_CHANNEL_5,
    .preamble_length = 128U,
    .pac_size = DW3000_PAC_SIZE_8,
    .tx_preamble_code = 9U,
    .rx_preamble_code = 9U,
    .sfd_type = DW3000_SFD_IEEE_4A,
    .data_rate = DW3000_DATA_RATE_6M8,
    .sfd_timeout = 129U,
    .sts_mode = DW3000_STS_OFF,
    .sts_length = 64U,
    .extended_phr = false,
    .tx_pulse_generator_delay = 0x34U,
    .tx_power = UINT32_C(0xFDFDFDFD),
    .tx_pulse_generator_count = 0U,
    .tx_antenna_delay = 0U,
    .rx_antenna_delay = 0U,
  },
  .pan_id = UINT16_C(0xDECA),
  .destination_address = UINT16_C(0xFFFF),
  .source_address = UINT16_C(0x0001),
};
