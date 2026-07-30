from __future__ import annotations

import struct

from hopwins.protocol.crc import crc32_bzip2
from hopwins.protocol.packets import V2_HEADER_LENGTH, PacketFlags, PacketType


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
