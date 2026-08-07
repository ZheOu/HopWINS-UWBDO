from __future__ import annotations

import struct

from hopwins.protocol.crc import crc32_bzip2
from hopwins.protocol.packets import (
    V2_HEADER_LENGTH,
    V3_HEADER_LENGTH,
    CirSource,
    PacketFlags,
    PacketType,
)


def build_v2_packet(
    packet_type: PacketType,
    *,
    capture_id: int = 7,
    chunk_index: int = 0,
    chunk_count: int = 1,
    payload: bytes = b"",
    flags: PacketFlags = (
        PacketFlags.RX_CRC_GOOD
        | PacketFlags.DIAGNOSTIC_OK
        | PacketFlags.CIR_VALID
        | PacketFlags.REGISTERS_OK
        | PacketFlags.REFERENCE_TIME_VALID
    ),
    capture_sample_offset: int = 100,
    payload_sample_offset: int = 100,
    payload_sample_count: int = 0,
    capture_sample_count: int = 0,
    raw_rx_timestamp: int | None = None,
) -> bytes:
    if raw_rx_timestamp is not None:
        flags |= PacketFlags.RAW_TIMESTAMP_VALID
    header = bytearray(V2_HEADER_LENGTH)
    header[0:4] = b"HCIR"
    header[4] = 2
    header[5] = int(packet_type)
    struct.pack_into("<H", header, 6, V2_HEADER_LENGTH)
    struct.pack_into("<H", header, 8, len(payload))
    struct.pack_into("<H", header, 10, int(flags))
    struct.pack_into("<I", header, 12, capture_id)
    struct.pack_into("<H", header, 16, chunk_index)
    struct.pack_into("<H", header, 18, chunk_count)
    struct.pack_into("<Q", header, 20, 0x123456789A)
    struct.pack_into("<I", header, 28, 0x00004000)
    struct.pack_into("<i", header, 32, -12345)
    struct.pack_into("<h", header, 36, -17)
    struct.pack_into("<H", header, 38, 23)
    struct.pack_into("<H", header, 40, 128 * 64 + 32)
    struct.pack_into("<H", header, 42, 133)
    struct.pack_into("<H", header, 44, 127)
    struct.pack_into("<H", header, 46, capture_sample_offset)
    struct.pack_into("<H", header, 48, payload_sample_offset)
    struct.pack_into("<H", header, 50, payload_sample_count)
    struct.pack_into("<H", header, 52, capture_sample_count)
    header[54] = 6
    header[55] = 1
    struct.pack_into("<h", header, 56, -64 * 256)
    struct.pack_into("<h", header, 58, -67 * 256)
    header[60] = 1
    header[61] = 1
    struct.pack_into("<H", header, 62, 0x4020)
    struct.pack_into("<I", header, 64, 1234)
    struct.pack_into("<I", header, 68, 1222)
    struct.pack_into("<I", header, 72, 0xABCDEF01)
    struct.pack_into("<I", header, 76, 0x10203040)
    struct.pack_into("<I", header, 80, 0x50607080)
    struct.pack_into("<I", header, 84, 0x11223344)
    struct.pack_into("<I", header, 88, 0x55667788)
    struct.pack_into("<I", header, 92, 1000)
    struct.pack_into("<I", header, 96, 101)
    struct.pack_into("<I", header, 100, 102)
    struct.pack_into("<I", header, 104, 103)
    struct.pack_into("<I", header, 108, 104)
    struct.pack_into("<I", header, 112, 105)
    struct.pack_into("<H", header, 116, 127 * 64)
    header[118] = 12
    header[119] = 3
    header[120] = 0
    header[121] = 0
    header[122] = 0
    if raw_rx_timestamp is not None:
        header[123:128] = (raw_rx_timestamp & ((1 << 40) - 1)).to_bytes(
            5,
            "little",
        )
    packet = bytes(header) + payload
    return packet + struct.pack("<I", crc32_bzip2(packet))


def build_v3_packet(
    packet_type: PacketType,
    *,
    capture_id: int = 7,
    cir_source: CirSource = CirSource.STS0,
    rf_port: int = 1,
    chunk_index: int = 0,
    chunk_count: int = 1,
    payload: bytes = b"",
    capture_sample_offset: int = 100,
    payload_sample_offset: int = 100,
    payload_sample_count: int = 0,
    capture_sample_count: int = 0,
    pdoa_q1_11: int = -512,
    tdoa_dtu: int = -33,
) -> bytes:
    base = build_v2_packet(
        packet_type,
        capture_id=capture_id,
        chunk_index=chunk_index,
        chunk_count=chunk_count,
        payload=payload,
        flags=(
            PacketFlags.RX_CRC_GOOD
            | PacketFlags.CIR_FULL_48BIT
            | PacketFlags.DIAGNOSTIC_OK
            | PacketFlags.CIR_VALID
            | PacketFlags.REGISTERS_OK
            | PacketFlags.REFERENCE_TIME_VALID
            | PacketFlags.PDOA_DIAGNOSTIC_VALID
        ),
        capture_sample_offset=capture_sample_offset,
        payload_sample_offset=payload_sample_offset,
        payload_sample_count=payload_sample_count,
        capture_sample_count=capture_sample_count,
        raw_rx_timestamp=0x1234560000,
    )
    header = bytearray(V3_HEADER_LENGTH)
    header[:V2_HEADER_LENGTH] = base[:V2_HEADER_LENGTH]
    header[4] = 3
    struct.pack_into("<H", header, 6, V3_HEADER_LENGTH)
    header[60] = rf_port
    header[128] = int(cir_source)
    header[129] = 2
    header[130] = rf_port
    header[131] = 0
    header[132:137] = (0x1234500001).to_bytes(5, "little")
    header[137:142] = (0x1234500011).to_bytes(5, "little")
    header[142:147] = (0x1234500022).to_bytes(5, "little")
    header[147] = 1
    struct.pack_into("<H", header, 148, 2)
    struct.pack_into("<H", header, 150, 3)
    struct.pack_into("<H", header, 152, 100)
    struct.pack_into("<H", header, 154, 200)
    struct.pack_into("<H", header, 156, 300)
    struct.pack_into("<h", header, 158, pdoa_q1_11)
    struct.pack_into("<q", header, 160, tdoa_dtu)
    packet = bytes(header) + payload
    return packet + struct.pack("<I", crc32_bzip2(packet))
