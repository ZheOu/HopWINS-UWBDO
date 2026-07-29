"""Recorded HCIR replay task."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

from hopwins.ui.cir_window import run_replay_monitor

if TYPE_CHECKING:
    from hopwins.tasks.registry import TaskContext


def run(path: str | Path, speed: float, rolling_window: int) -> int:
    return run_replay_monitor(path, speed, rolling_window)


def run_configured(context: TaskContext) -> int:
    configured_path = context.config.task_text(
        context.task_name,
        "path",
        fallback="latest",
    )
    path = (
        context.config.latest_capture_path()
        if configured_path.casefold() == "latest"
        else context.config.resolve_path(configured_path)
    )
    if not path.is_file():
        raise ValueError(f"capture file not found: {path}")
    speed = context.config.task_float(
        context.task_name,
        "speed",
        fallback=1.0,
    )
    rolling_window = context.config.task_int(
        context.task_name,
        "rolling_window",
        fallback=200,
    )
    if speed <= 0 or rolling_window <= 0:
        raise ValueError("speed and rolling_window must be positive")
    print(f"task={context.task_name} replay={path} speed={speed:g}")
    return run(path, speed, rolling_window)
