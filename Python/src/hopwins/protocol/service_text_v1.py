"""Typed MCU service records shared by workflow-specific datasets."""

from __future__ import annotations

from dataclasses import dataclass

from hopwins.protocol.common_text import parse_fields


@dataclass(frozen=True, slots=True)
class UwbTxRecord:
    result: str
    status: int
    sequence: int
    frame_length: int
    scheduled_time: int
    transmit_timestamp: int
    late_count: int


@dataclass(frozen=True, slots=True)
class UwbRxHealthRecord:
    enabled: bool
    pending: bool
    queued_captures: int
    received_count: int
    error_count: int
    crc_error_count: int
    recovery_count: int
    watchdog_count: int
    queue_full_count: int
    uart_error_count: int


def parse_uwb_tx(line: str) -> UwbTxRecord | None:
    if line.startswith("UWB TX OK,"):
        result = "OK"
    elif line.startswith("UWB TX ERROR,"):
        result = "ERROR"
    else:
        return None
    fields = parse_fields(line)
    required = {"STATUS", "SEQ", "LEN", "SCHED", "TX_TS", "LATE"}
    if not required.issubset(fields):
        return None
    try:
        return UwbTxRecord(
            result=result,
            status=_integer(fields["STATUS"]),
            sequence=_integer(fields["SEQ"]),
            frame_length=_integer(fields["LEN"]),
            scheduled_time=_integer(fields["SCHED"]),
            transmit_timestamp=_integer(fields["TX_TS"]),
            late_count=_integer(fields["LATE"]),
        )
    except ValueError:
        return None


def parse_uwb_rx_health(line: str) -> UwbRxHealthRecord | None:
    if not line.startswith("UWB RX HEALTH,"):
        return None
    fields = parse_fields(line)
    required = {
        "ENABLE",
        "PENDING",
        "QUEUED",
        "RX",
        "ERR",
        "CRC_ERR",
        "RECOVERY",
        "WATCHDOG",
        "QFULL",
        "UART_ERR",
    }
    if not required.issubset(fields):
        return None
    try:
        return UwbRxHealthRecord(
            enabled=_boolean(fields["ENABLE"]),
            pending=_boolean(fields["PENDING"]),
            queued_captures=_integer(fields["QUEUED"]),
            received_count=_integer(fields["RX"]),
            error_count=_integer(fields["ERR"]),
            crc_error_count=_integer(fields["CRC_ERR"]),
            recovery_count=_integer(fields["RECOVERY"]),
            watchdog_count=_integer(fields["WATCHDOG"]),
            queue_full_count=_integer(fields["QFULL"]),
            uart_error_count=_integer(fields["UART_ERR"]),
        )
    except ValueError:
        return None


def _integer(value: str) -> int:
    return int(value, 0) if value.startswith(("0x", "-0x", "+0x")) else int(value)


def _boolean(value: str) -> bool:
    parsed = _integer(value)
    if parsed not in (0, 1):
        raise ValueError("expected firmware boolean")
    return bool(parsed)
