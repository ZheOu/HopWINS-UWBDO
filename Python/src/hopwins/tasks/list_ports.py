"""Serial port discovery task."""

from __future__ import annotations

from typing import TYPE_CHECKING

from hopwins.transport.serial_reader import SerialPortInfo, list_serial_ports

if TYPE_CHECKING:
    from hopwins.tasks.registry import TaskContext


def run() -> int:
    ports = list_serial_ports()
    if not ports:
        print("No serial ports found")
        return 0
    for port in ports:
        print(_format_port(port))
    return 0


def run_configured(_context: TaskContext) -> int:
    return run()


def _format_port(port: SerialPortInfo) -> str:
    vid_pid = (
        f"{port.vid:04X}:{port.pid:04X}"
        if port.vid is not None and port.pid is not None
        else "-"
    )
    serial_number = port.serial_number or "-"
    return (
        f"{port.device}\t{port.description}\t"
        f"VID:PID={vid_pid}\tSER={serial_number}\t{port.hardware_id}"
    )
