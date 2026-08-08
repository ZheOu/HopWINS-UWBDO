"""Create an algorithm-ready dataset from online or recorded HCIR v3 output."""

from __future__ import annotations

from collections import OrderedDict
from dataclasses import asdict
from pathlib import Path
from typing import cast

from hopwins.capture.assembler import CirCapture
from hopwins.capture.pairing import DualCirPairAssembler
from hopwins.core.pipeline import run_pipeline
from hopwins.core.prompts import PromptField, TaskPromptSpec
from hopwins.core.records import Record
from hopwins.core.session import ExperimentSession
from hopwins.core.task import TaskContext
from hopwins.io.replay import ReplaySource
from hopwins.io.serial import SerialSource
from hopwins.protocol import create_decoder
from hopwins.protocol.common_text import FirmwareProfile
from hopwins.protocol.service_text_v1 import UwbRxHealthRecord
from hopwins.storage.dataset import DualCirDatasetWriter
from hopwins.storage.reader import resolve_raw_stream
from hopwins.storage.recorder import SessionRecorder
from hopwins.tasks.service_dataset import UWB_RX_HEALTH_FIELDS

PROMPT = TaskPromptSpec(
    default_label="sts-dual-rx",
    default_notes="Paired STS0/RF1 and STS1/RF2 CIR dataset.",
    fields=(
        PromptField(
            "duration_s",
            "Recording duration in seconds (0 = until Ctrl+C)",
            0.0,
            "float",
        ),
        PromptField(
            "flush_every_pairs",
            "Flush files after this many complete CIR pairs",
            10,
            "int",
        ),
        PromptField(
            "maximum_pending_pairs",
            "Maximum incomplete CIR pairs kept in memory",
            32,
            "int",
        ),
    ),
)


class DualCirDatasetTask:
    def __init__(
        self,
        *,
        expected_board: str | None,
        expected_role: str | None,
        label: str | None,
        flush_every_pairs: int,
        maximum_pending_pairs: int,
    ) -> None:
        self.expected_board = expected_board
        self.expected_role = expected_role
        self.label = label
        self.flush_every_pairs = flush_every_pairs
        self.pairer = DualCirPairAssembler(maximum_pending_pairs)
        self._times: OrderedDict[tuple[int, int], tuple[int, int]] = OrderedDict()
        self._maximum_times = maximum_pending_pairs * 2
        self.writer: DualCirDatasetWriter | None = None
        self.channels = 0
        self.rejected_channels = 0
        self.text_lines = 0
        self.rx_health_rows = 0

    def start(self, context: TaskContext) -> None:
        assert context.recorder is not None
        self.writer = DualCirDatasetWriter(
            context.recorder,
            label=self.label,
            flush_every_pairs=self.flush_every_pairs,
        )
        context.recorder.ensure_table("uwb_rx_health", UWB_RX_HEALTH_FIELDS)
        context.recorder.write_event(
            "task.started",
            task=context.task_name,
            mode=context.mode,
        )

    def handle(self, record: Record, context: TaskContext) -> None:
        assert context.recorder is not None
        if record.kind == "text.line":
            self.text_lines += 1
            context.recorder.write_event(
                "serial.text",
                host_utc_ns=record.host_utc_ns,
                host_monotonic_ns=record.host_monotonic_ns,
                text=str(record.payload),
            )
            return
        if record.kind == "firmware.profile":
            profile = cast(FirmwareProfile, record.payload)
            self._accept_profile(profile)
            assert context.session is not None
            context.session.update(firmware_profile=asdict(profile))
            return
        if record.kind == "uwb.rx_health":
            row = asdict(cast(UwbRxHealthRecord, record.payload))
            row.update(
                {
                    "host_utc_ns": record.host_utc_ns,
                    "host_monotonic_ns": record.host_monotonic_ns,
                }
            )
            context.recorder.write_row(
                "uwb_rx_health",
                UWB_RX_HEALTH_FIELDS,
                row,
            )
            self.rx_health_rows += 1
            return
        if record.kind != "cir.capture":
            return

        capture = cast(CirCapture, record.payload)
        if not capture.has_cir:
            self.rejected_channels += 1
            return
        self.channels += 1
        key = (capture.header.capture_id, int(capture.header.cir_source))
        self._times[key] = (record.host_utc_ns, record.host_monotonic_ns)
        self._times.move_to_end(key)
        while len(self._times) > self._maximum_times:
            self._times.popitem(last=False)

        pair = self.pairer.add(capture)
        if pair is None:
            return
        assert self.writer is not None
        fallback = (record.host_utc_ns, record.host_monotonic_ns)
        sts0_time = self._times.pop((pair.capture_id, 1), fallback)
        sts1_time = self._times.pop((pair.capture_id, 2), fallback)
        self.writer.write_pair(
            pair,
            sts0_host_utc_ns=sts0_time[0],
            sts0_host_monotonic_ns=sts0_time[1],
            sts1_host_utc_ns=sts1_time[0],
            sts1_host_monotonic_ns=sts1_time[1],
        )

    def stop(self, context: TaskContext) -> dict[str, object]:
        if self.writer is not None:
            self.writer.close()
        if context.recorder is not None:
            context.recorder.write_event("task.stopped", task=context.task_name)
        return self.statistics()

    def statistics(self) -> dict[str, object]:
        return {
            "text_lines": self.text_lines,
            "complete_channels": self.channels,
            "rejected_channels": self.rejected_channels,
            "uwb_rx_health_rows": self.rx_health_rows,
            "complete_pairs": self.writer.pair_count if self.writer else 0,
            "total_iq_samples": (self.writer.total_iq_samples if self.writer else 0),
            "pairing": asdict(self.pairer.statistics),
        }

    def _accept_profile(self, profile: FirmwareProfile) -> None:
        mismatches = []
        if self.expected_board and profile.board != self.expected_board:
            mismatches.append(f"board={profile.board}, expected {self.expected_board}")
        if self.expected_role and profile.role != self.expected_role:
            mismatches.append(f"role={profile.role}, expected {self.expected_role}")
        if mismatches:
            raise RuntimeError("firmware profile mismatch: " + "; ".join(mismatches))


def run_configured(context: TaskContext) -> int:
    protocol_name = context.parameter_text("protocol", fallback="hcir_v3")
    duration_s = context.parameter_float("duration_s", fallback=0.0)
    flush_every_pairs = context.parameter_int("flush_every_pairs", fallback=10)
    maximum_pending_pairs = context.parameter_int(
        "maximum_pending_pairs",
        fallback=32,
    )
    if duration_s < 0:
        raise ValueError("duration_s must not be negative")
    if flush_every_pairs <= 0 or maximum_pending_pairs <= 0:
        raise ValueError("dataset flush and pending-pair settings must be positive")

    device = context.require_device() if context.mode == "online" else None
    if context.mode == "online":
        assert device is not None
        port = context.resolve_port(device)
        input_path = None
        source_name = f"serial:{port}"
    elif context.mode == "offline":
        port = None
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
    task = DualCirDatasetTask(
        expected_board=device.expected_board if device else None,
        expected_role=device.expected_role if device else None,
        label=context.label,
        flush_every_pairs=flush_every_pairs,
        maximum_pending_pairs=maximum_pending_pairs,
    )
    print(f"dataset={session.path} mode={context.mode} source={source_name}")

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
        statistics = task.statistics()
    except Exception:
        status = "failed"
        statistics = task.statistics()
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
        f"status={status} pairs={statistics.get('complete_pairs', 0)} "
        f"iq_samples={statistics.get('total_iq_samples', 0)} "
        f"raw_bytes={recorder.bytes_written}"
    )
    return 0


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
    raise RuntimeError("could not allocate a unique dataset session directory")
