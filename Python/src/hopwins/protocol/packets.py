"""Typed HCIR packet models and fixed-header decoding."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum, IntFlag

MAGIC = b"HCIR"
V1_HEADER_LENGTH = 60
V2_HEADER_LENGTH = 128
MIN_HEADER_LENGTH = V1_HEADER_LENGTH
MAX_HEADER_LENGTH = 512
MAX_PAYLOAD_LENGTH = 8192
CRC_LENGTH = 4
INVALID_Q8_8 = -32768
DW_TIMESTAMP_BITS = 40
DW_TIMESTAMP_MASK = (1 << DW_TIMESTAMP_BITS) - 1


class PacketType(IntEnum):
    RX_FRAME = 1
    CIR_DATA = 2


class PacketFlags(IntFlag):
    RX_CRC_GOOD = 0x0001
    RANGING_FRAME = 0x0002
    CIR_FULL_48BIT = 0x0004
    DIAGNOSTIC_OK = 0x0008
    CIR_VALID = 0x0010
    REGISTERS_OK = 0x0020
    REFERENCE_TIME_VALID = 0x0040
    RAW_TIMESTAMP_VALID = 0x0080


@dataclass(frozen=True, slots=True)
class HcirHeader:
    version: int
    packet_type: PacketType
    header_length: int
    payload_length: int
    flags: PacketFlags
    capture_id: int
    chunk_index: int
    chunk_count: int
    rx_timestamp: int
    system_status_low: int
    carrier_integrator: int
    clock_offset: int
    frame_length: int
    first_path_index_q10_6: int
    peak_index: int
    accumulated_symbols: int
    capture_sample_offset: int
    payload_sample_offset: int
    payload_sample_count: int
    capture_sample_count: int
    bytes_per_sample: int
    cir_format: int
    rssi_q8_8: int
    first_path_power_q8_8: int
    rf_port: int = 0
    reference_time_source: int = 0
    rx_antenna_delay: int = 0
    mcu_system_time_ms: int = 0
    reference_time_ms: int = 0
    dw_system_time_hi32: int = 0
    system_status_high: int = 0
    rx_finfo: int = 0
    cia_diag_0: int = 0
    cia_diag_1: int = 0
    cir_power: int = 0
    first_path_amplitude_1: int = 0
    first_path_amplitude_2: int = 0
    first_path_amplitude_3: int = 0
    peak_amplitude: int = 0
    first_path_threshold: int = 0
    early_first_path_index_q10_6: int = 0
    early_first_path_confidence_q0_4: int = 0
    dgc_decision: int = 0
    diagnostic_status: int = 0
    cir_status: int = 0
    register_status: int = 0
    raw_rx_timestamp: int = 0

    @property
    def first_path_index(self) -> float:
        return self.first_path_index_q10_6 / 64.0

    @property
    def early_first_path_index(self) -> float:
        return self.early_first_path_index_q10_6 / 64.0

    @property
    def rssi_dbm(self) -> float | None:
        return _q8_8_or_none(self.rssi_q8_8)

    @property
    def first_path_power_dbm(self) -> float | None:
        return _q8_8_or_none(self.first_path_power_q8_8)

    @property
    def raw_rx_timestamp_valid(self) -> bool:
        return bool(self.flags & PacketFlags.RAW_TIMESTAMP_VALID)

    @property
    def cia_correction_dtu(self) -> int | None:
        if not self.raw_rx_timestamp_valid:
            return None
        value = (
            self.rx_timestamp - self.raw_rx_timestamp + self.rx_antenna_delay
        ) & DW_TIMESTAMP_MASK
        if value & (1 << (DW_TIMESTAMP_BITS - 1)):
            return value - (1 << DW_TIMESTAMP_BITS)
        return value

    @property
    def cia_fpi_offset_dtu(self) -> int | None:
        correction = self.cia_correction_dtu
        if correction is None:
            return None
        return correction - self.first_path_index_q10_6


@dataclass(frozen=True, slots=True)
class HcirPacket:
    header: HcirHeader
    payload: bytes
    crc: int
    raw: bytes


def parse_hcir_packet(raw: bytes) -> HcirPacket:
    if len(raw) < MIN_HEADER_LENGTH + CRC_LENGTH or raw[:4] != MAGIC:
        raise ValueError("not a complete HCIR packet")

    version = raw[4]
    header_length = _u16(raw, 6)
    payload_length = _u16(raw, 8)
    expected_length = header_length + payload_length + CRC_LENGTH
    if header_length < MIN_HEADER_LENGTH or len(raw) != expected_length:
        raise ValueError("invalid HCIR packet length")
    if version >= 2 and header_length < V2_HEADER_LENGTH:
        raise ValueError("HCIR v2 header is too short")

    try:
        packet_type = PacketType(raw[5])
    except ValueError as exc:
        raise ValueError(f"unknown HCIR packet type {raw[5]}") from exc

    values: dict[str, int] = {}
    if version >= 2:
        values = {
            "rf_port": raw[60],
            "reference_time_source": raw[61],
            "rx_antenna_delay": _u16(raw, 62),
            "mcu_system_time_ms": _u32(raw, 64),
            "reference_time_ms": _u32(raw, 68),
            "dw_system_time_hi32": _u32(raw, 72),
            "system_status_high": _u32(raw, 76),
            "rx_finfo": _u32(raw, 80),
            "cia_diag_0": _u32(raw, 84),
            "cia_diag_1": _u32(raw, 88),
            "cir_power": _u32(raw, 92),
            "first_path_amplitude_1": _u32(raw, 96),
            "first_path_amplitude_2": _u32(raw, 100),
            "first_path_amplitude_3": _u32(raw, 104),
            "peak_amplitude": _u32(raw, 108),
            "first_path_threshold": _u32(raw, 112),
            "early_first_path_index_q10_6": _u16(raw, 116),
            "early_first_path_confidence_q0_4": raw[118],
            "dgc_decision": raw[119],
            "diagnostic_status": _i8(raw, 120),
            "cir_status": _i8(raw, 121),
            "register_status": _i8(raw, 122),
            "raw_rx_timestamp": _u40(raw, 123),
        }

    header = HcirHeader(
        version=version,
        packet_type=packet_type,
        header_length=header_length,
        payload_length=payload_length,
        flags=PacketFlags(_u16(raw, 10)),
        capture_id=_u32(raw, 12),
        chunk_index=_u16(raw, 16),
        chunk_count=_u16(raw, 18),
        rx_timestamp=_u64(raw, 20) & DW_TIMESTAMP_MASK,
        system_status_low=_u32(raw, 28),
        carrier_integrator=_i32(raw, 32),
        clock_offset=_i16(raw, 36),
        frame_length=_u16(raw, 38),
        first_path_index_q10_6=_u16(raw, 40),
        peak_index=_u16(raw, 42),
        accumulated_symbols=_u16(raw, 44),
        capture_sample_offset=_u16(raw, 46),
        payload_sample_offset=_u16(raw, 48),
        payload_sample_count=_u16(raw, 50),
        capture_sample_count=_u16(raw, 52),
        bytes_per_sample=raw[54],
        cir_format=raw[55],
        rssi_q8_8=_i16(raw, 56),
        first_path_power_q8_8=_i16(raw, 58),
        **values,
    )
    payload = raw[header_length : header_length + payload_length]
    return HcirPacket(
        header=header,
        payload=payload,
        crc=_u32(raw, header_length + payload_length),
        raw=raw,
    )


def _q8_8_or_none(value: int) -> float | None:
    return None if value == INVALID_Q8_8 else value / 256.0


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _i16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<h", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def _u40(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 5], "little")


def _i8(data: bytes, offset: int) -> int:
    return struct.unpack_from("<b", data, offset)[0]
