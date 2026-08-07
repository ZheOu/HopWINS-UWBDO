/**
  ******************************************************************************
  * @file           : hcir_protocol.h
  * @brief          : Versioned HCIR binary UART framing
  ******************************************************************************
  */

#ifndef HOPWINS_HCIR_PROTOCOL_H
#define HOPWINS_HCIR_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "board.h"
#include "uwb_service.h"

#define HCIR_SAMPLES_PER_CHUNK 80U

typedef enum {
  HCIR_PROTOCOL_VERSION_2 = 2,
  HCIR_PROTOCOL_VERSION_3 = 3,
} hcir_protocol_version_t;

typedef enum {
  HCIR_PACKET_RX_FRAME = 1,
  HCIR_PACKET_CIR_DATA = 2,
} hcir_packet_type_t;

board_status_t hcir_protocol_send_frame(
    const uwb_service_cir_capture_t *capture,
    hcir_protocol_version_t version);
board_status_t hcir_protocol_send_samples(
    const uwb_service_cir_capture_t *capture,
    hcir_protocol_version_t version,
    uint16_t chunk_index,
    uint16_t chunk_count,
    uint16_t relative_sample_offset,
    uint16_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_HCIR_PROTOCOL_H */
