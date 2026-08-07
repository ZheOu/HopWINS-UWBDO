"""Serial discovery and the online pipeline source."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass

import serial
from serial.tools import list_ports

from hopwins.core.records import ByteChunk


@dataclass(frozen=True, slots=True)
class SerialPortInfo:
    device: str
    description: str
    hardware_id: str
    vid: int | None = None
    pid: int | None = None
    serial_number: str | None = None


def list_serial_ports() -> list[SerialPortInfo]:
    return [
        SerialPortInfo(
            port.device,
            port.description,
            port.hwid,
            port.vid,
            port.pid,
            port.serial_number,
        )
        for port in list_ports.comports()
    ]


def resolve_serial_port(
    port_specification: str,
    *,
    vid: int | None = None,
    pid: int | None = None,
    serial_number: str | None = None,
    description_contains: str | None = None,
    available_ports: Sequence[SerialPortInfo] | None = None,
) -> str:
    if port_specification.casefold() != "auto":
        return port_specification
    ports = list(list_serial_ports() if available_ports is None else available_ports)
    matches = [
        port
        for port in ports
        if (vid is None or port.vid == vid)
        and (pid is None or port.pid == pid)
        and (serial_number is None or port.serial_number == serial_number)
        and (
            description_contains is None
            or description_contains.casefold() in port.description.casefold()
        )
    ]
    if len(matches) == 1:
        return matches[0].device
    filters = (
        f"VID={_format_hex(vid)}, PID={_format_hex(pid)}, "
        f"serial={serial_number or '*'}, "
        f"description={description_contains or '*'}"
    )
    if not matches:
        visible = ", ".join(port.device for port in ports) or "none"
        raise RuntimeError(
            f"no serial port matches auto selection ({filters}); "
            f"available ports: {visible}"
        )
    choices = ", ".join(
        f"{port.device} (serial={port.serial_number or '-'})" for port in matches
    )
    raise RuntimeError(
        "auto serial selection is ambiguous; set port or serial_number "
        f"for one of: {choices}"
    )


class SerialSource:
    def __init__(
        self,
        port: str,
        baudrate: int,
        *,
        timeout_s: float = 0.1,
        read_size: int = 65536,
    ) -> None:
        self.port = port
        self.name = f"serial:{port}"
        self._stream = serial.Serial(
            port,
            baudrate,
            timeout=timeout_s,
            write_timeout=0.5,
        )
        self._read_size = read_size

    def read(self) -> ByteChunk | None:
        if not self._stream.is_open:
            return None
        return ByteChunk.now(self._stream.read(self._read_size), self.name)

    def close(self) -> None:
        if self._stream.is_open:
            self._stream.close()


def _format_hex(value: int | None) -> str:
    return "*" if value is None else f"0x{value:04X}"
