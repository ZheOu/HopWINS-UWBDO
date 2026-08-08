"""Generic online/offline session capture without task-specific algorithms."""

from __future__ import annotations

from dataclasses import asdict
from pathlib import Path
from typing import cast

from hopwins.capture.assembler import CirCapture
from hopwins.core.pipeline import run_pipeline
from hopwins.core.prompts import PromptField, TaskPromptSpec
from hopwins.core.records import Record
from hopwins.core.session import ExperimentSession
from hopwins.core.task import TaskContext
from hopwins.io.replay import ReplaySource
from hopwins.io.serial import SerialSource
from hopwins.protocol import create_decoder
from hopwins.protocol.common_text import FirmwareProfile
from hopwins.protocol.do_text_v1 import DoTrackConfig, DoTrackRecord
from hopwins.storage.reader import resolve_raw_stream
from hopwins.storage.recorder import SessionRecorder

PROMPT = TaskPromptSpec(
    default_label="session-capture",
    default_notes="Lossless serial capture retained for offline replay.",
    fields=(
        PromptField(
            "duration_s",
            "Recording duration in seconds (0 = until Ctrl+C)",
            0.0,
            "float",
        ),
    ),
)

DO_TRACK_FIELDS = (
    "host_utc_ns",
    "host_monotonic_ns",
    "result",
    "reference_sequence",
    "sequence",
    "interval_count",
    "leader_delta_dtu",
    "follower_delta_dtu",
    "error_ppb",
    "command_ppb",
    "clock_status",
    "observations",
    "updates",
    "rejects",
)

CAPTURE_FIELDS = (
    "host_utc_ns",
    "host_monotonic_ns",
    "protocol_version",
    "capture_id",
    "cir_source",
    "cir_group_size",
    "rx_timestamp",
    "raw_rx_timestamp",
    "cia_correction_dtu",
    "first_path_index",
    "peak_index",
    "rssi_dbm",
    "first_path_power_dbm",
    "carrier_integrator",
    "clock_offset",
    "rf_port",
    "cir_source_rf_port",
    "reference_time_ms",
    "ipatov_timestamp",
    "sts0_timestamp",
    "sts1_timestamp",
    "pdoa_diagnostic_status",
    "ipatov_status",
    "sts0_status",
    "sts1_status",
    "ipatov_phase",
    "sts0_phase",
    "sts1_phase",
    "pdoa_radians",
    "pdoa_q1_11",
    "tdoa_dtu",
    "cir_samples",
    "received_chunks",
    "expected_chunks",
)


class SessionCaptureTask:
    do_track_fields = DO_TRACK_FIELDS
    capture_fields = CAPTURE_FIELDS

    def __init__(
        self,
        expected_board: str | None,
        expected_role: str | None,
        *,
        record_do_track: bool = True,
        record_captures: bool = True,
    ) -> None:
        self.expected_board = expected_board
        self.expected_role = expected_role
        self.record_do_track = record_do_track
        self.record_captures = record_captures
        self.text_lines = 0
        self.do_records = 0
        self.captures = 0
        self.profiles = 0

    def start(self, context: TaskContext) -> None:
        assert context.recorder is not None
        context.recorder.write_event(
            "task.started",
            task=context.task_name,
            mode=context.mode,
        )

    def handle(self, record: Record, context: TaskContext) -> None:
        assert context.recorder is not None
        recorder = context.recorder
        if record.kind == "text.line":
            self.text_lines += 1
            recorder.write_event(
                "serial.text",
                host_monotonic_ns=record.host_monotonic_ns,
                host_utc_ns=record.host_utc_ns,
                text=str(record.payload),
            )
            return
        if record.kind == "firmware.profile":
            profile = cast(FirmwareProfile, record.payload)
            self._validate_profile(profile)
            self.profiles += 1
            assert context.session is not None
            context.session.update(firmware_profile=asdict(profile))
            return
        if record.kind == "do.track.config":
            if not self.record_do_track:
                return
            config = cast(DoTrackConfig, record.payload)
            recorder.write_event("do.track.config", config=asdict(config))
            return
        if record.kind == "do.track":
            if self.record_do_track:
                self._write_do_track(
                    cast(DoTrackRecord, record.payload),
                    record,
                    recorder,
                )
            return
        if record.kind == "cir.capture":
            if self.record_captures:
                self._write_capture(
                    cast(CirCapture, record.payload),
                    record,
                    recorder,
                )

    def stop(self, context: TaskContext) -> dict[str, object]:
        if context.recorder is not None:
            context.recorder.write_event("task.stopped", task=context.task_name)
        return self.statistics()

    def statistics(self) -> dict[str, object]:
        return {
            "text_lines": self.text_lines,
            "firmware_profiles": self.profiles,
            "do_track_records": self.do_records,
            "complete_captures": self.captures,
        }

    def _validate_profile(self, profile: FirmwareProfile) -> None:
        mismatches = []
        if self.expected_board and profile.board != self.expected_board:
            mismatches.append(
                f"board={profile.board}, expected {self.expected_board}"
            )
        if self.expected_role and profile.role != self.expected_role:
            mismatches.append(f"role={profile.role}, expected {self.expected_role}")
        if mismatches:
            raise RuntimeError("firmware profile mismatch: " + "; ".join(mismatches))

    def _write_do_track(
        self,
        value: DoTrackRecord,
        record: Record,
        recorder: SessionRecorder,
    ) -> None:
        row = asdict(value)
        row.update(
            {
                "host_utc_ns": record.host_utc_ns,
                "host_monotonic_ns": record.host_monotonic_ns,
            }
        )
        recorder.write_row("do_track", DO_TRACK_FIELDS, row)
        self.do_records += 1

    def _write_capture(
        self,
        capture: CirCapture,
        record: Record,
        recorder: SessionRecorder,
    ) -> None:
        header = capture.header
        row = {
            "host_utc_ns": record.host_utc_ns,
            "host_monotonic_ns": record.host_monotonic_ns,
            "protocol_version": header.version,
            "capture_id": header.capture_id,
            "cir_source": header.cir_source.name,
            "cir_group_size": header.cir_group_size,
            "rx_timestamp": header.rx_timestamp,
            "raw_rx_timestamp": (
                header.raw_rx_timestamp if header.raw_rx_timestamp_valid else None
            ),
            "cia_correction_dtu": header.cia_correction_dtu,
            "first_path_index": header.first_path_index,
            "peak_index": header.peak_index,
            "rssi_dbm": header.rssi_dbm,
            "first_path_power_dbm": header.first_path_power_dbm,
            "carrier_integrator": header.carrier_integrator,
            "clock_offset": header.clock_offset,
            "rf_port": header.rf_port,
            "cir_source_rf_port": header.cir_source_rf_port,
            "reference_time_ms": header.reference_time_ms,
            "ipatov_timestamp": header.ipatov_timestamp,
            "sts0_timestamp": header.sts0_timestamp,
            "sts1_timestamp": header.sts1_timestamp,
            "pdoa_diagnostic_status": header.pdoa_diagnostic_status,
            "ipatov_status": header.ipatov_status,
            "sts0_status": header.sts0_status,
            "sts1_status": header.sts1_status,
            "ipatov_phase": header.ipatov_phase,
            "sts0_phase": header.sts0_phase,
            "sts1_phase": header.sts1_phase,
            "pdoa_radians": header.pdoa_radians,
            "pdoa_q1_11": header.pdoa_q1_11,
            "tdoa_dtu": header.tdoa_dtu,
            "cir_samples": header.capture_sample_count,
            "received_chunks": capture.received_chunks,
            "expected_chunks": capture.expected_chunks,
        }
        recorder.write_row("captures", CAPTURE_FIELDS, row)
        self.captures += 1


def run_configured(context: TaskContext) -> int:
    parameters = context.parameters
    protocol_name = context.parameter_text(
        "protocol",
        fallback=context.spec.protocol or "auto",
    )
    duration_s = context.parameter_float("duration_s", fallback=0.0)
    if duration_s < 0:
        raise ValueError("duration_s must not be negative")

    device = (
        context.config.device(context.device_name)
        if context.mode == "online"
        else None
    )
    port: str | None = None
    input_path: Path | None = None
    source_name: str
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
        parameters=parameters,
        effective_config=context.config.snapshot(),
        label=context.label,
        notes=context.notes,
        source=source_name,
    )
    recorder = SessionRecorder(session)
    context.session = session
    context.recorder = recorder
    task = SessionCaptureTask(
        device.expected_board if device else None,
        device.expected_role if device else None,
    )
    print(f"session={session.path} mode={context.mode} source={source_name}")

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
        f"status={status} raw_bytes={recorder.bytes_written} "
        f"do_records={task.do_records} captures={task.captures}"
    )
    return 0


def _resolve_input(
    context: TaskContext,
) -> Path:
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
    raise RuntimeError("could not allocate a unique session directory")
