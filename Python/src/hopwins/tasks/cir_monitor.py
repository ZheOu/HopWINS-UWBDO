"""Live CIR monitor task."""

from __future__ import annotations

from collections.abc import Mapping
from pathlib import Path
from typing import TYPE_CHECKING

from hopwins.ui.cir_window import run_live_monitor

if TYPE_CHECKING:
    from hopwins.tasks.registry import TaskContext


def run(
    port: str,
    baudrate: int,
    record_path: str | Path | None,
    rolling_window: int,
    *,
    timeout_s: float = 0.1,
    expected_board: str | None = None,
    expected_role: str | None = None,
    recorder_metadata: Mapping[str, object] | None = None,
) -> int:
    return run_live_monitor(
        port,
        baudrate,
        record_path,
        rolling_window,
        timeout_s=timeout_s,
        expected_board=expected_board,
        expected_role=expected_role,
        recorder_metadata=recorder_metadata,
    )


def run_configured(context: TaskContext) -> int:
    device = context.require_device()
    port = context.resolve_port(device)
    rolling_window = context.config.task_int(
        context.task_name,
        "rolling_window",
        fallback=200,
    )
    if rolling_window <= 0:
        raise ValueError("rolling_window must be positive")

    record_path: Path | None = None
    if context.config.task_bool(
        context.task_name,
        "record",
        fallback=True,
    ):
        configured_path = context.config.task_text(
            context.task_name,
            "record_path",
        )
        record_path = (
            context.config.resolve_path(configured_path)
            if configured_path
            else context.config.new_capture_path(context.task_name, device.name)
        )

    output_text = str(record_path) if record_path else "disabled"
    print(
        f"task={context.task_name} device={device.name} "
        f"port={port} baudrate={device.baudrate} output={output_text}"
    )
    return run(
        port,
        device.baudrate,
        record_path,
        rolling_window,
        timeout_s=device.timeout_s,
        expected_board=device.expected_board,
        expected_role=device.expected_role,
        recorder_metadata=context.recorder_metadata(device, port),
    )
