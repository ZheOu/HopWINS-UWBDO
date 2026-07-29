/**
  ******************************************************************************
  * @file           : cir_protocol.h
  * @brief          : Binary UART framing for UWB frames and CIR captures
  ******************************************************************************
  */

#ifndef HOPWINS_CIR_PROTOCOL_H
#define HOPWINS_CIR_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "board.h"
#include "uwb_service.h"

#define CIR_PROTOCOL_SAMPLES_PER_CHUNK 80U

typedef enum {
  CIR_PROTOCOL_PACKET_RX_FRAME = 1,
  CIR_PROTOCOL_PACKET_CIR_DATA = 2,
} cir_protocol_packet_type_t;

board_status_t cir_protocol_send_frame(
    const uwb_service_cir_capture_t *capture);
board_status_t cir_protocol_send_samples(
    const uwb_service_cir_capture_t *capture,
    uint16_t chunk_index,
    uint16_t chunk_count,
    uint16_t relative_sample_offset,
    uint16_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_CIR_PROTOCOL_H */
