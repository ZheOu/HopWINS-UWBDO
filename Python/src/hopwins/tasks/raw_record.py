"""Headless lossless serial recording task."""

from __future__ import annotations

import queue
from collections.abc import Mapping
from pathlib import Path
from typing import TYPE_CHECKING

from hopwins.transport.serial_reader import (
    CaptureEvent,
    ProfileEvent,
    SerialWorker,
    WorkerError,
    WorkerEvent,
)

if TYPE_CHECKING:
    from hopwins.tasks.registry import TaskContext


def run(
    port: str,
    baudrate: int,
    output: str | Path,
    *,
    timeout_s: float = 0.1,
    expected_board: str | None = None,
    expected_role: str | None = None,
    recorder_metadata: Mapping[str, object] | None = None,
) -> int:
    events: queue.Queue[WorkerEvent] = queue.Queue(maxsize=4096)
    worker = SerialWorker(
        port,
        baudrate,
        events,
        output,
        timeout_s=timeout_s,
        expected_board=expected_board,
        expected_role=expected_role,
        recorder_metadata=recorder_metadata,
    )
    worker.start()
    capture_count = 0
    try:
        while True:
            try:
                event = events.get(timeout=0.5)
            except queue.Empty:
                continue
            if isinstance(event, ProfileEvent):
                print(
                    f"profile board={event.profile.board} "
                    f"role={event.profile.role} build={event.profile.build}"
                )
            elif isinstance(event, CaptureEvent):
                capture_count += 1
                if capture_count == 1 or capture_count % 100 == 0:
                    print(
                        f"captures={capture_count} "
                        f"last_id={event.capture.header.capture_id}"
                    )
            elif isinstance(event, WorkerError):
                print(f"serial error: {event.message}")
                return 1
    except KeyboardInterrupt:
        return 0
    finally:
        worker.stop()


def run_configured(context: TaskContext) -> int:
    device = context.require_device()
    port = context.resolve_port(device)
    configured_path = context.config.task_text(
        context.task_name,
        "output",
    )
    output = (
        context.config.resolve_path(configured_path)
        if configured_path
        else context.config.new_capture_path(context.task_name, device.name)
    )
    print(
        f"task={context.task_name} device={device.name} "
        f"port={port} baudrate={device.baudrate} output={output}"
    )
    return run(
        port,
        device.baudrate,
        output,
        timeout_s=device.timeout_s,
        expected_board=device.expected_board,
        expected_role=device.expected_role,
        recorder_metadata=context.recorder_metadata(device, port),
    )
