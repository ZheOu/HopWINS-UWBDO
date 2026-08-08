"""Shared online/offline Session runner for record-oriented dataset tasks."""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from hopwins.config import DeviceConfig
from hopwins.core.pipeline import run_pipeline
from hopwins.core.session import ExperimentSession
from hopwins.core.task import RecordTask, TaskContext
from hopwins.io.replay import ReplaySource
from hopwins.io.serial import SerialSource
from hopwins.protocol import create_decoder
from hopwins.storage.reader import resolve_raw_stream
from hopwins.storage.recorder import SessionRecorder

TaskFactory = Callable[[DeviceConfig | None], RecordTask]


def run_session_pipeline(
    context: TaskContext,
    task_factory: TaskFactory,
    *,
    output_name: str = "session",
) -> int:
    protocol_name = context.parameter_text(
        "protocol",
        fallback=context.spec.protocol or "auto",
    )
    duration_s = context.parameter_float("duration_s", fallback=0.0)
    if duration_s < 0:
        raise ValueError("duration_s must not be negative")

    device = context.require_device() if context.mode == "online" else None
    port: str | None = None
    input_path: Path | None = None
    if context.mode == "online":
        assert device is not None
        port = context.resolve_port(device)
        source_name = f"serial:{port}"
    elif context.mode == "offline":
        input_path = _resolve_input(context)
        source_name = f"replay:{resolve_raw_stream(input_path)}"
    else:
        raise ValueError("mode must be 'online' or 'offline'")

    session_path = _unique_session_path(
        context.config.new_session_path(
            context.task_name,
            device.name if device else context.device_name,
            context.label,
        )
    )
    session = ExperimentSession.create(
        session_path,
        task=context.task_name,
        category=context.spec.category,
        mode=context.mode,
        protocol=protocol_name,
        device=device.name if device else context.device_name,
        parameters=context.parameters,
        effective_config=context.config.snapshot(),
        label=context.label,
        notes=context.notes,
        source=source_name,
    )
    recorder = SessionRecorder(session)
    context.session = session
    context.recorder = recorder
    task = task_factory(device)
    print(f"{output_name}={session.path} mode={context.mode} source={source_name}")

    status = "complete"
    statistics: dict[str, object] = {}
    try:
        decoder = create_decoder(protocol_name)
        if context.mode == "online":
            assert device is not None and port is not None
            source = SerialSource(
                port,
                device.baudrate,
                timeout_s=device.timeout_s,
            )
        else:
            assert input_path is not None
            source = ReplaySource(
                input_path,
                speed=context.parameter_float("replay_speed", fallback=0.0),
            )
        statistics = run_pipeline(
            source,
            decoder,
            task,
            context,
            duration_s=duration_s or None,
            record_raw=(
                context.mode == "online"
                or context.parameter_bool("copy_raw", fallback=False)
            ),
        )
    except KeyboardInterrupt:
        status = "interrupted"
        statistics = _task_statistics(task)
    except Exception:
        status = "failed"
        statistics = _task_statistics(task)
        raise
    finally:
        statistics.update(
            {
                "raw_bytes": recorder.bytes_written,
                "raw_chunks": recorder.chunks_written,
            }
        )
        recorder.close()
        session.finish(status, statistics)

    print(
        f"status={status} raw_bytes={recorder.bytes_written} "
        f"records={statistics.get('records', 0)}"
    )
    return 0


def _task_statistics(task: RecordTask) -> dict[str, object]:
    statistics = getattr(task, "statistics", None)
    return dict(statistics()) if callable(statistics) else {}


def _resolve_input(context: TaskContext) -> Path:
    if context.input_path is not None:
        return context.input_path
    configured = context.parameter_text("path", fallback="latest")
    if configured.casefold() == "latest":
        return context.config.latest_capture_path()
    return context.config.resolve_path(configured)


def _unique_session_path(path: Path) -> Path:
    if not path.exists():
        return path
    for index in range(2, 1000):
        candidate = path.with_name(f"{path.name}-{index:02d}")
        if not candidate.exists():
            return candidate
    raise RuntimeError("could not allocate a unique Session directory")
