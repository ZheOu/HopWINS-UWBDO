/**
  ******************************************************************************
  * @file           : cir_protocol.c
  * @brief          : Binary UART framing for UWB frames and CIR captures
  ******************************************************************************
  */

#include "cir_protocol.h"

#include <string.h>

#define CIR_PROTOCOL_VERSION             2U
#define CIR_PROTOCOL_HEADER_LEN          128U
#define CIR_PROTOCOL_CRC_LEN             4U
#define CIR_PROTOCOL_MAX_PAYLOAD_LEN     DW3000_RX_FRAME_MAX_LEN
#define CIR_PROTOCOL_MAX_PACKET_LEN \
  (CIR_PROTOCOL_HEADER_LEN + CIR_PROTOCOL_MAX_PAYLOAD_LEN + \
   CIR_PROTOCOL_CRC_LEN)

#define CIR_PROTOCOL_FLAG_RX_CRC_GOOD    UINT16_C(0x0001)
#define CIR_PROTOCOL_FLAG_RANGING_FRAME  UINT16_C(0x0002)
#define CIR_PROTOCOL_FLAG_CIR_FULL_48BIT UINT16_C(0x0004)
#define CIR_PROTOCOL_FLAG_DIAGNOSTIC_OK  UINT16_C(0x0008)
#define CIR_PROTOCOL_FLAG_CIR_VALID      UINT16_C(0x0010)
#define CIR_PROTOCOL_FLAG_REGISTERS_OK   UINT16_C(0x0020)
#define CIR_PROTOCOL_FLAG_REFERENCE_TIME UINT16_C(0x0040)

#define CIR_PROTOCOL_FORMAT_I24_Q24_LE   1U
#define CIR_PROTOCOL_REFERENCE_TIM2_MS   1U

static uint8_t s_packet[CIR_PROTOCOL_MAX_PACKET_LEN];

static void write_u16_le(uint8_t *destination, uint16_t value);
static void write_u32_le(uint8_t *destination, uint32_t value);
static void write_u64_le(uint8_t *destination, uint64_t value);
static board_status_t send_packet(
    const uwb_service_cir_capture_t *capture,
    cir_protocol_packet_type_t packet_type,
    uint16_t chunk_index,
    uint16_t chunk_count,
    uint16_t payload_sample_offset,
    uint16_t payload_sample_count,
    const uint8_t *payload,
    uint16_t payload_len);

board_status_t cir_protocol_send_frame(
    const uwb_service_cir_capture_t *capture)
{
  if (capture == NULL) {
    return BOARD_BAD_ARG;
  }

  return send_packet(
      capture,
      CIR_PROTOCOL_PACKET_RX_FRAME,
      0U,
      1U,
      capture->cir_sample_offset,
      0U,
      capture->frame,
      capture->frame_len);
}

board_status_t cir_protocol_send_samples(
    const uwb_service_cir_capture_t *capture,
    uint16_t chunk_index,
    uint16_t chunk_count,
    uint16_t relative_sample_offset,
    uint16_t sample_count)
{
  uint32_t byte_offset;
  uint32_t payload_len;

  if ((capture == NULL) || (sample_count == 0U) ||
      (capture->cir_sample_bytes == 0U) ||
      ((uint32_t)relative_sample_offset + sample_count >
       capture->cir_sample_count)) {
    return BOARD_BAD_ARG;
  }

  byte_offset =
      (uint32_t)relative_sample_offset * capture->cir_sample_bytes;
  payload_len = (uint32_t)sample_count * capture->cir_sample_bytes;
  if ((byte_offset + payload_len > capture->cir_data_len) ||
      (payload_len > UINT16_MAX)) {
    return BOARD_BAD_ARG;
  }

  return send_packet(
      capture,
      CIR_PROTOCOL_PACKET_CIR_DATA,
      chunk_index,
      chunk_count,
      (uint16_t)(capture->cir_sample_offset + relative_sample_offset),
      sample_count,
      &capture->cir_data[byte_offset],
      (uint16_t)payload_len);
}

static board_status_t send_packet(
    const uwb_service_cir_capture_t *capture,
    cir_protocol_packet_type_t packet_type,
    uint16_t chunk_index,
    uint16_t chunk_count,
    uint16_t payload_sample_offset,
    uint16_t payload_sample_count,
    const uint8_t *payload,
    uint16_t payload_len)
{
  uint16_t flags = CIR_PROTOCOL_FLAG_RX_CRC_GOOD;
  uint32_t packet_len =
      CIR_PROTOCOL_HEADER_LEN + payload_len + CIR_PROTOCOL_CRC_LEN;
  uint32_t crc;
  board_status_t status;

  if ((capture == NULL) || ((payload == NULL) && (payload_len != 0U)) ||
      (packet_len > sizeof(s_packet)) || (chunk_count == 0U) ||
      (chunk_index >= chunk_count)) {
    return BOARD_BAD_ARG;
  }
  if (board_pc_tx_available() < packet_len) {
    return BOARD_BUSY;
  }
  if (capture->ranging_frame) {
    flags |= CIR_PROTOCOL_FLAG_RANGING_FRAME;
  }
  if (capture->diagnostic_status == DW3000_STATUS_OK) {
    flags |= CIR_PROTOCOL_FLAG_DIAGNOSTIC_OK;
  }
  if (capture->cir_status == DW3000_STATUS_OK) {
    flags |= CIR_PROTOCOL_FLAG_CIR_VALID |
             CIR_PROTOCOL_FLAG_CIR_FULL_48BIT;
  }
  if (capture->register_status == DW3000_STATUS_OK) {
    flags |= CIR_PROTOCOL_FLAG_REGISTERS_OK;
  }
  if (capture->reference_time_valid) {
    flags |= CIR_PROTOCOL_FLAG_REFERENCE_TIME;
  }

  memset(s_packet, 0, CIR_PROTOCOL_HEADER_LEN);
  memcpy(&s_packet[0], "HCIR", 4U);
  s_packet[4] = CIR_PROTOCOL_VERSION;
  s_packet[5] = (uint8_t)packet_type;
  write_u16_le(&s_packet[6], CIR_PROTOCOL_HEADER_LEN);
  write_u16_le(&s_packet[8], payload_len);
  write_u16_le(&s_packet[10], flags);
  write_u32_le(&s_packet[12], capture->capture_id);
  write_u16_le(&s_packet[16], chunk_index);
  write_u16_le(&s_packet[18], chunk_count);
  write_u64_le(&s_packet[20], capture->receive_timestamp);
  write_u32_le(&s_packet[28], capture->system_status);
  write_u32_le(&s_packet[32], (uint32_t)capture->carrier_integrator);
  write_u16_le(&s_packet[36], (uint16_t)capture->clock_offset);
  write_u16_le(&s_packet[38], capture->frame_len);
  write_u16_le(
      &s_packet[40],
      capture->diagnostic.first_path_index);
  write_u16_le(&s_packet[42], capture->diagnostic.peak_index);
  write_u16_le(
      &s_packet[44],
      capture->diagnostic.accumulated_symbols);
  write_u16_le(&s_packet[46], capture->cir_sample_offset);
  write_u16_le(&s_packet[48], payload_sample_offset);
  write_u16_le(&s_packet[50], payload_sample_count);
  write_u16_le(&s_packet[52], capture->cir_sample_count);
  s_packet[54] = capture->cir_sample_bytes;
  s_packet[55] = CIR_PROTOCOL_FORMAT_I24_Q24_LE;
  write_u16_le(&s_packet[56], (uint16_t)capture->diagnostic.rssi_q8_8);
  write_u16_le(
      &s_packet[58],
      (uint16_t)capture->diagnostic.first_path_power_q8_8);
  s_packet[60] = (uint8_t)capture->rf_port;
  s_packet[61] = capture->reference_time_valid
                     ? CIR_PROTOCOL_REFERENCE_TIM2_MS
                     : 0U;
  write_u16_le(&s_packet[62], capture->rx_antenna_delay);
  write_u32_le(&s_packet[64], capture->mcu_system_time_ms);
  write_u32_le(&s_packet[68], capture->reference_time_ms);
  write_u32_le(
      &s_packet[72],
      capture->register_snapshot.system_time_hi32);
  write_u32_le(
      &s_packet[76],
      capture->register_snapshot.system_status_high);
  write_u32_le(&s_packet[80], capture->register_snapshot.rx_finfo);
  write_u32_le(&s_packet[84], capture->register_snapshot.cia_diag_0);
  write_u32_le(&s_packet[88], capture->register_snapshot.cia_diag_1);
  write_u32_le(&s_packet[92], capture->diagnostic.power);
  write_u32_le(
      &s_packet[96],
      capture->diagnostic.first_path_amplitude_1);
  write_u32_le(
      &s_packet[100],
      capture->diagnostic.first_path_amplitude_2);
  write_u32_le(
      &s_packet[104],
      capture->diagnostic.first_path_amplitude_3);
  write_u32_le(&s_packet[108], capture->diagnostic.peak_amplitude);
  write_u32_le(
      &s_packet[112],
      capture->diagnostic.first_path_threshold);
  write_u16_le(
      &s_packet[116],
      capture->diagnostic.early_first_path_index);
  s_packet[118] = capture->diagnostic.early_first_path_confidence;
  s_packet[119] = capture->register_snapshot.dgc_decision;
  s_packet[120] = (uint8_t)(int8_t)capture->diagnostic_status;
  s_packet[121] = (uint8_t)(int8_t)capture->cir_status;
  s_packet[122] = (uint8_t)(int8_t)capture->register_status;

  if (payload_len != 0U) {
    memcpy(&s_packet[CIR_PROTOCOL_HEADER_LEN], payload, payload_len);
  }

  status = board_crc32_calculate(
      s_packet,
      CIR_PROTOCOL_HEADER_LEN + payload_len,
      &crc);
  if (status != BOARD_OK) {
    return status;
  }
  write_u32_le(
      &s_packet[CIR_PROTOCOL_HEADER_LEN + payload_len],
      crc);
  return board_pc_transmit(s_packet, packet_len);
}

static void write_u16_le(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8U);
  destination[2] = (uint8_t)(value >> 16U);
  destination[3] = (uint8_t)(value >> 24U);
}

static void write_u64_le(uint8_t *destination, uint64_t value)
{
  for (uint32_t i = 0U; i < sizeof(value); i++) {
    destination[i] = (uint8_t)(value >> (8U * i));
  }
}
