/**
  ******************************************************************************
  * @file           : hcir_v2_protocol.h
  * @brief          : HCIR v2 binary UART framing
  ******************************************************************************
  */

#ifndef HOPWINS_HCIR_V2_PROTOCOL_H
#define HOPWINS_HCIR_V2_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "board.h"
#include "uwb_service.h"

#define HCIR_V2_SAMPLES_PER_CHUNK 80U

typedef enum {
  HCIR_V2_PACKET_RX_FRAME = 1,
  HCIR_V2_PACKET_CIR_DATA = 2,
} hcir_v2_packet_type_t;

board_status_t hcir_v2_protocol_send_frame(
    const uwb_service_cir_capture_t *capture);
board_status_t hcir_v2_protocol_send_samples(
    const uwb_service_cir_capture_t *capture,
    uint16_t chunk_index,
    uint16_t chunk_count,
    uint16_t relative_sample_offset,
    uint16_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_HCIR_V2_PROTOCOL_H */
