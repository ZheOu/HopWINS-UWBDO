from __future__ import annotations

import csv
import tempfile
import unittest
from pathlib import Path
from typing import cast

from hopwins.config import ProjectConfig
from hopwins.core.records import ByteChunk
from hopwins.core.session import ExperimentSession
from hopwins.core.task import TaskContext, TaskSpec
from hopwins.protocol.hcir import HcirDecoder
from hopwins.storage.recorder import SessionRecorder
from hopwins.tasks.service_dataset import (
    WorkflowDatasetProfile,
    WorkflowDatasetTask,
)


class WorkflowDatasetTests(unittest.TestCase):
    def test_tx_workflow_writes_text_and_structured_rows(self) -> None:
        line = (
            b"UWB TX OK, STATUS=0x00, SEQ=0x0000002A, LEN=0x0017, "
            b"SCHED=0x00123456, TX_TS=0x123456789A, LATE=0x00000001\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path, context, recorder = _context(Path(directory), "do_leader_dataset")
            task = WorkflowDatasetTask(
                expected_board=None,
                expected_role=None,
                profile=WorkflowDatasetProfile("DO-Leader", record_tx=True),
            )
            _feed(task, context, line)
            task.stop(context)
            recorder.close()

            tx_rows = _rows(path / "records" / "uwb_tx.csv")
            text_rows = _rows(path / "records" / "serial_text.csv")

        self.assertEqual(tx_rows[0]["sequence"], "42")
        self.assertEqual(tx_rows[0]["result"], "OK")
        self.assertEqual(text_rows[0]["prefix"], "UWB TX OK")

    def test_follower_workflow_writes_health_and_do_tracking(self) -> None:
        lines = (
            b"UWB RX HEALTH, ENABLE=1, PENDING=1, QUEUED=0x00, "
            b"RX=0x0000002A, ERR=0x00000001, CRC_ERR=0x00000000, "
            b"RECOVERY=0x00000002, WATCHDOG=0x00000003, QFULL=0x00000000, "
            b"UART_ERR=0x00000004\n"
            b"DO TRACK, RESULT=UPDATE, SEQ0=0x1, SEQ1=0x15, N=20, "
            b"TX_DT=0x100, RX_DT=0x101, ERR_PPB=10, CMD_PPB=-10, "
            b"CLK_STATUS=0x00, OBS=1, UPDATES=0x1, REJECT=0x0\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path, context, recorder = _context(Path(directory), "do_follower_dataset")
            task = WorkflowDatasetTask(
                expected_board=None,
                expected_role=None,
                profile=WorkflowDatasetProfile(
                    "DO-Follower",
                    record_rx_health=True,
                    record_do_track=True,
                    record_captures=True,
                ),
            )
            _feed(task, context, lines)
            task.stop(context)
            recorder.close()

            health_rows = _rows(path / "records" / "uwb_rx_health.csv")
            track_rows = _rows(path / "records" / "do_track.csv")

        self.assertEqual(health_rows[0]["received_count"], "42")
        self.assertEqual(track_rows[0]["error_ppb"], "10")


def _context(
    root: Path,
    task_name: str,
) -> tuple[Path, TaskContext, SessionRecorder]:
    path = root / "session"
    session = ExperimentSession.create(
        path,
        task=task_name,
        category="dataset",
        mode="offline",
        protocol="hcir",
        device=None,
        parameters={},
        effective_config={},
        label="test",
        notes=None,
        source="test",
    )
    recorder = SessionRecorder(session)
    context = TaskContext(
        config=cast(ProjectConfig, object()),
        spec=TaskSpec(task_name, "dataset", "test", "test"),
        mode="offline",
        session=session,
        recorder=recorder,
    )
    return path, context, recorder


def _feed(
    task: WorkflowDatasetTask,
    context: TaskContext,
    data: bytes,
) -> None:
    task.start(context)
    decoder = HcirDecoder()
    for record in decoder.feed(ByteChunk(data, 10, 20, "test")):
        task.handle(record, context)


def _rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


if __name__ == "__main__":
    unittest.main()
