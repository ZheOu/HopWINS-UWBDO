"""Raw serial-link probe that does not parse the firmware protocol."""

from __future__ import annotations

import time
from contextlib import nullcontext
from pathlib import Path
from typing import TYPE_CHECKING, BinaryIO

import serial

if TYPE_CHECKING:
    from contextlib import AbstractContextManager

    from hopwins.tasks.registry import TaskContext


def run(
    port: str,
    baudrate: int,
    *,
    duration_s: float = 10.0,
    preview_bytes: int = 256,
    report_interval_s: float = 1.0,
    timeout_s: float = 0.1,
    output: str | Path | None = None,
) -> int:
    if baudrate <= 0:
        raise ValueError("baudrate must be positive")
    if duration_s < 0:
        raise ValueError("duration_s must not be negative")
    if preview_bytes < 0:
        raise ValueError("preview_bytes must not be negative")
    if report_interval_s <= 0 or timeout_s <= 0:
        raise ValueError("report_interval_s and timeout_s must be positive")

    output_path = Path(output) if output is not None else None
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)

    total_bytes = 0
    preview = bytearray()
    started_at = time.monotonic()
    last_report_at = started_at
    last_report_bytes = 0
    interrupted = False

    print(
        f"serial probe: port={port} baudrate={baudrate} "
        f"duration={'until Ctrl-C' if duration_s == 0 else f'{duration_s:g}s'}"
    )
    print("opening port; reset or power-cycle the target if boot output is needed")

    try:
        with serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout_s,
            write_timeout=0.5,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        ) as stream:
            output_context: AbstractContextManager[BinaryIO | None]
            output_context = (
                output_path.open("wb") if output_path is not None else nullcontext()
            )
            with output_context as raw_output:
                while True:
                    now = time.monotonic()
                    if duration_s > 0 and now - started_at >= duration_s:
                        break

                    data = stream.read(65536)
                    now = time.monotonic()
                    if data:
                        total_bytes += len(data)
                        if raw_output is not None:
                            raw_output.write(data)
                        remaining_preview = preview_bytes - len(preview)
                        if remaining_preview > 0:
                            preview.extend(data[:remaining_preview])

                    if now - last_report_at >= report_interval_s:
                        interval_s = now - last_report_at
                        interval_bytes = total_bytes - last_report_bytes
                        print(
                            f"elapsed={now - started_at:6.1f}s "
                            f"bytes={total_bytes:10d} "
                            f"rate={interval_bytes / interval_s:10.1f} B/s"
                        )
                        last_report_at = now
                        last_report_bytes = total_bytes
    except KeyboardInterrupt:
        interrupted = True

    elapsed_s = max(time.monotonic() - started_at, 1e-9)
    result = "data-received" if total_bytes else "no-data"
    print(
        f"serial probe complete: result={result} bytes={total_bytes} "
        f"elapsed={elapsed_s:.2f}s average={total_bytes / elapsed_s:.1f} B/s"
    )
    if output_path is not None:
        print(f"raw output: {output_path}")
    if preview:
        print(f"first {len(preview)} byte(s):")
        print(format_hex_preview(preview))
    elif (total_bytes == 0) and not interrupted:
        print("no bytes arrived; check port ownership, baudrate, wiring, and firmware")
    return 0 if total_bytes else 1


def run_configured(context: TaskContext) -> int:
    device = context.require_device()
    port = context.resolve_port(device)
    duration_s = context.config.task_float(
        context.task_name,
        "duration_s",
        fallback=10.0,
    )
    preview_bytes = context.config.task_int(
        context.task_name,
        "preview_bytes",
        fallback=256,
    )
    report_interval_s = context.config.task_float(
        context.task_name,
        "report_interval_s",
        fallback=1.0,
    )
    save = context.config.task_bool(
        context.task_name,
        "save",
        fallback=True,
    )
    configured_output = context.config.task_text(
        context.task_name,
        "output",
    )
    output: Path | None = None
    if save:
        output = (
            context.config.resolve_path(configured_output)
            if configured_output
            else context.config.new_capture_path(
                context.task_name,
                device.name,
            ).with_suffix(".bin")
        )

    print(
        f"task={context.task_name} device={device.name} "
        f"port={port} baudrate={device.baudrate}"
    )
    return run(
        port,
        device.baudrate,
        duration_s=duration_s,
        preview_bytes=preview_bytes,
        report_interval_s=report_interval_s,
        timeout_s=device.timeout_s,
        output=output,
    )


def format_hex_preview(data: bytes | bytearray, width: int = 16) -> str:
    if width <= 0:
        raise ValueError("width must be positive")
    lines = []
    for offset in range(0, len(data), width):
        chunk = data[offset : offset + width]
        hexadecimal = " ".join(f"{value:02X}" for value in chunk)
        printable = "".join(
            chr(value) if 0x20 <= value <= 0x7E else "." for value in chunk
        )
        lines.append(
            f"{offset:08X}  {hexadecimal:<{width * 3 - 1}}  |{printable}|"
        )
    return "\n".join(lines)
