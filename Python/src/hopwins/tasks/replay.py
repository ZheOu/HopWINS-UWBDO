"""Recorded HCIR replay task."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

from hopwins.storage.reader import resolve_raw_stream
from hopwins.ui.cir_window import run_replay_monitor

if TYPE_CHECKING:
    from hopwins.core.task import TaskContext


def run(path: str | Path, speed: float, rolling_window: int) -> int:
    return run_replay_monitor(path, speed, rolling_window)


def run_configured(context: TaskContext) -> int:
    configured_path = context.parameter_text("path", fallback="latest")
    selected_path = context.input_path
    if selected_path is None:
        selected_path = (
            context.config.latest_capture_path()
            if configured_path.casefold() == "latest"
            else context.config.resolve_path(configured_path)
        )
    path = resolve_raw_stream(selected_path)
    speed = context.parameter_float("speed", fallback=1.0)
    rolling_window = context.parameter_int("rolling_window", fallback=200)
    if speed <= 0 or rolling_window <= 0:
        raise ValueError("speed and rolling_window must be positive")
    print(f"task={context.task_name} replay={path} speed={speed:g}")
    return run(path, speed, rolling_window)
