"""Shared tables used by MCU workflow-specific dataset tasks."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import TYPE_CHECKING, cast

from hopwins.core.records import Record
from hopwins.protocol.service_text_v1 import UwbRxHealthRecord, UwbTxRecord
from hopwins.tasks.session_capture import SessionCaptureTask
from hopwins.tasks.session_runtime import run_session_pipeline

if TYPE_CHECKING:
    from hopwins.config import DeviceConfig
    from hopwins.core.task import TaskContext
    from hopwins.storage.recorder import SessionRecorder

SERIAL_TEXT_FIELDS = (
    "host_utc_ns",
    "host_monotonic_ns",
    "prefix",
    "text",
)

UWB_TX_FIELDS = (
    "host_utc_ns",
    "host_monotonic_ns",
    "result",
    "status",
    "sequence",
    "frame_length",
    "scheduled_time",
    "transmit_timestamp",
    "late_count",
)

UWB_RX_HEALTH_FIELDS = (
    "host_utc_ns",
    "host_monotonic_ns",
    "enabled",
    "pending",
    "queued_captures",
    "received_count",
    "error_count",
    "crc_error_count",
    "recovery_count",
    "watchdog_count",
    "queue_full_count",
    "uart_error_count",
)


@dataclass(frozen=True, slots=True)
class WorkflowDatasetProfile:
    workflow: str
    record_tx: bool = False
    record_rx_health: bool = False
    record_do_track: bool = False
    record_captures: bool = False


class WorkflowDatasetTask(SessionCaptureTask):
    def __init__(
        self,
        *,
        expected_board: str | None,
        expected_role: str | None,
        profile: WorkflowDatasetProfile,
    ) -> None:
        super().__init__(
            expected_board,
            expected_role,
            record_do_track=profile.record_do_track,
            record_captures=profile.record_captures,
        )
        self.profile = profile
        self.serial_rows = 0
        self.tx_rows = 0
        self.rx_health_rows = 0

    def start(self, context: TaskContext) -> None:
        super().start(context)
        assert context.recorder is not None
        recorder = context.recorder
        recorder.ensure_table("serial_text", SERIAL_TEXT_FIELDS)
        if self.profile.record_tx:
            recorder.ensure_table("uwb_tx", UWB_TX_FIELDS)
        if self.profile.record_rx_health:
            recorder.ensure_table("uwb_rx_health", UWB_RX_HEALTH_FIELDS)
        if self.profile.record_do_track:
            recorder.ensure_table("do_track", self.do_track_fields)
        if self.profile.record_captures:
            recorder.ensure_table("captures", self.capture_fields)
        recorder.write_event(
            "dataset.profile",
            workflow=self.profile.workflow,
            tables=self._table_names(),
        )

    def handle(self, record: Record, context: TaskContext) -> None:
        super().handle(record, context)
        assert context.recorder is not None
        recorder = context.recorder
        if record.kind == "text.line":
            text = str(record.payload)
            recorder.write_row(
                "serial_text",
                SERIAL_TEXT_FIELDS,
                {
                    "host_utc_ns": record.host_utc_ns,
                    "host_monotonic_ns": record.host_monotonic_ns,
                    "prefix": text.partition(",")[0].strip(),
                    "text": text,
                },
            )
            self.serial_rows += 1
        elif record.kind == "uwb.tx" and self.profile.record_tx:
            self._write_typed_row(
                "uwb_tx",
                UWB_TX_FIELDS,
                cast(UwbTxRecord, record.payload),
                record,
                recorder,
            )
            self.tx_rows += 1
        elif record.kind == "uwb.rx_health" and self.profile.record_rx_health:
            self._write_typed_row(
                "uwb_rx_health",
                UWB_RX_HEALTH_FIELDS,
                cast(UwbRxHealthRecord, record.payload),
                record,
                recorder,
            )
            self.rx_health_rows += 1

    def statistics(self) -> dict[str, object]:
        values = super().statistics()
        values.update(
            {
                "workflow": self.profile.workflow,
                "serial_text_rows": self.serial_rows,
                "uwb_tx_rows": self.tx_rows,
                "uwb_rx_health_rows": self.rx_health_rows,
            }
        )
        return values

    def _table_names(self) -> list[str]:
        names = ["serial_text"]
        if self.profile.record_tx:
            names.append("uwb_tx")
        if self.profile.record_rx_health:
            names.append("uwb_rx_health")
        if self.profile.record_do_track:
            names.append("do_track")
        if self.profile.record_captures:
            names.append("captures")
        return names

    @staticmethod
    def _write_typed_row(
        name: str,
        fields: tuple[str, ...],
        value: UwbTxRecord | UwbRxHealthRecord,
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
        recorder.write_row(name, fields, row)


def run_workflow_dataset(
    context: TaskContext,
    profile: WorkflowDatasetProfile,
) -> int:
    def factory(device: DeviceConfig | None) -> WorkflowDatasetTask:
        return WorkflowDatasetTask(
            expected_board=device.expected_board if device else None,
            expected_role=device.expected_role if device else None,
            profile=profile,
        )

    return run_session_pipeline(context, factory, output_name="dataset")
