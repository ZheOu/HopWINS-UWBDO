"""Online recording and offline replay for HCIR v3 STS dual CIR pairs."""

from __future__ import annotations

import queue
from dataclasses import asdict
from pathlib import Path
from typing import TYPE_CHECKING

from hopwins.core.session import ExperimentSession
from hopwins.io.workers import ReplayWorker, SerialWorker, WorkerEvent
from hopwins.storage.reader import resolve_raw_stream
from hopwins.storage.recorder import SessionRawSink, SessionRecorder
from hopwins.ui.dual_cir_window import run_dual_monitor

if TYPE_CHECKING:
    from hopwins.core.task import TaskContext


def run_configured(context: TaskContext) -> int:
    refresh_hz = context.parameter_int("refresh_hz", fallback=30)
    if refresh_hz <= 0:
        raise ValueError("refresh_hz must be positive")

    events: queue.Queue[WorkerEvent] = queue.Queue(maxsize=4096)
    if context.mode == "offline":
        return _run_offline(context, events, refresh_hz)
    if context.mode != "online":
        raise ValueError("mode must be 'online' or 'offline'")
    return _run_online(context, events, refresh_hz)


def _run_online(
    context: TaskContext,
    events: queue.Queue[WorkerEvent],
    refresh_hz: int,
) -> int:
    device = context.require_device()
    port = context.resolve_port(device)
    source_name = f"serial:{port}"
    session: ExperimentSession | None = None
    recorder: SessionRecorder | None = None
    raw_sink: SessionRawSink | None = None

    if context.parameter_bool("record", fallback=True):
        session_path = _unique_session_path(
            context.config.new_session_path(
                context.task_name,
                device.name,
                context.label,
            )
        )
        session = ExperimentSession.create(
            session_path,
            task=context.task_name,
            category=context.spec.category,
            mode="online",
            protocol="hcir_v3",
            device=device.name,
            parameters=context.parameters,
            effective_config=context.config.snapshot(),
            label=context.label,
            notes=context.notes,
            source=source_name,
        )
        recorder = SessionRecorder(session)
        recorder.write_event("task.started", task=context.task_name, mode="online")
        raw_sink = SessionRawSink(recorder, session, source_name)
        context.session = session
        context.recorder = recorder
        print(f"session={session.path}")

    worker = SerialWorker(
        port,
        device.baudrate,
        events,
        timeout_s=device.timeout_s,
        expected_board=device.expected_board,
        expected_role=device.expected_role,
        raw_sink=raw_sink,
    )
    print(
        f"task={context.task_name} mode=online source={source_name} "
        f"record={'enabled' if recorder else 'disabled'}"
    )
    status = "complete"
    result = 0
    try:
        result = run_dual_monitor(worker, events, source_name, refresh_hz)
        if worker.last_error is not None:
            status = "failed"
            result = 1
    except KeyboardInterrupt:
        status = "interrupted"
    except Exception:
        status = "failed"
        raise
    finally:
        worker.stop()
        if recorder is not None and session is not None:
            statistics = _worker_statistics(worker, recorder)
            if worker.last_error is not None:
                statistics["worker_error"] = worker.last_error
            recorder.write_event("task.stopped", task=context.task_name, status=status)
            recorder.close()
            session.finish(status, statistics)
    return result


def _run_offline(
    context: TaskContext,
    events: queue.Queue[WorkerEvent],
    refresh_hz: int,
) -> int:
    path = _resolve_input(context)
    speed = context.parameter_float("replay_speed", fallback=1.0)
    if speed <= 0:
        raise ValueError("replay_speed must be positive")
    worker = ReplayWorker(path, events, speed)
    print(
        f"task={context.task_name} mode=offline source={worker.path} "
        f"speed={speed:g}x"
    )
    result = run_dual_monitor(worker, events, str(worker.path), refresh_hz)
    if worker.last_error is not None:
        raise RuntimeError(worker.last_error)
    return result


def _resolve_input(context: TaskContext) -> Path:
    if context.input_path is not None:
        return resolve_raw_stream(context.input_path)
    configured = context.parameter_text("path", fallback="latest")
    selected = (
        context.config.latest_capture_path()
        if configured.casefold() == "latest"
        else context.config.resolve_path(configured)
    )
    return resolve_raw_stream(selected)


def _worker_statistics(
    worker: SerialWorker,
    recorder: SessionRecorder,
) -> dict[str, object]:
    completed_channels = worker.assembler.statistics.completed_captures
    return {
        "raw_bytes": recorder.bytes_written,
        "raw_chunks": recorder.chunks_written,
        "complete_channels": completed_channels,
        "estimated_complete_pairs": completed_channels // 2,
        "parser": asdict(worker.parser.statistics),
        "assembler": asdict(worker.assembler.statistics),
    }


def _unique_session_path(path: Path) -> Path:
    if not path.exists():
        return path
    for index in range(2, 1000):
        candidate = path.with_name(f"{path.name}-{index:02d}")
        if not candidate.exists():
            return candidate
    raise RuntimeError("could not allocate a unique session directory")
