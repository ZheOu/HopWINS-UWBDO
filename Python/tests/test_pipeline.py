from __future__ import annotations

import unittest
from typing import cast

from hopwins.config import ProjectConfig
from hopwins.core.pipeline import run_pipeline
from hopwins.core.records import ByteChunk, Record
from hopwins.core.task import TaskContext, TaskSpec
from hopwins.storage.recorder import SessionRecorder


class _Recorder:
    def __init__(self) -> None:
        self.chunks: list[ByteChunk] = []

    def write_chunk(self, chunk: ByteChunk) -> None:
        self.chunks.append(chunk)


class _Source:
    name = "memory"

    def __init__(self) -> None:
        self._chunks = [ByteChunk.now(b"one", self.name)]
        self.closed = False

    def read(self) -> ByteChunk | None:
        return self._chunks.pop() if self._chunks else None

    def close(self) -> None:
        self.closed = True


class _Decoder:
    name = "test"

    def feed(self, chunk: ByteChunk) -> list[Record]:
        return [
            Record(
                "test",
                "test.v1",
                chunk.data,
                chunk.host_monotonic_ns,
                chunk.host_utc_ns,
            )
        ]

    def flush(self) -> list[Record]:
        return []

    def statistics(self) -> dict[str, object]:
        return {"decoded": 1}


class _Task:
    def __init__(self, *, fail: bool = False) -> None:
        self.fail = fail
        self.started = False
        self.stopped = False

    def start(self, context: TaskContext) -> None:
        self.started = True

    def handle(self, record: Record, context: TaskContext) -> None:
        if self.fail:
            raise RuntimeError("task failure")

    def stop(self, context: TaskContext) -> dict[str, object]:
        self.stopped = True
        return {"handled": 1}


def _context(recorder: _Recorder) -> TaskContext:
    return TaskContext(
        config=cast(ProjectConfig, object()),
        spec=TaskSpec("test", "test", "test", "test"),
        recorder=cast(SessionRecorder, recorder),
    )


class PipelineTests(unittest.TestCase):
    def test_records_raw_before_processing_and_closes_lifecycle(self) -> None:
        recorder = _Recorder()
        source = _Source()
        task = _Task()

        result = run_pipeline(source, _Decoder(), task, _context(recorder))

        self.assertEqual([chunk.data for chunk in recorder.chunks], [b"one"])
        self.assertTrue(task.started)
        self.assertTrue(task.stopped)
        self.assertTrue(source.closed)
        self.assertEqual(result["records"], 1)

    def test_stops_task_and_source_when_processing_fails(self) -> None:
        recorder = _Recorder()
        source = _Source()
        task = _Task(fail=True)

        with self.assertRaisesRegex(RuntimeError, "task failure"):
            run_pipeline(source, _Decoder(), task, _context(recorder))

        self.assertTrue(task.stopped)
        self.assertTrue(source.closed)


if __name__ == "__main__":
    unittest.main()
