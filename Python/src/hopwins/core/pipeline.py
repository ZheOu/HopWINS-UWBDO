"""Shared online/offline record-processing pipeline."""

from __future__ import annotations

from time import monotonic
from typing import Protocol

from hopwins.core.records import ByteChunk, Record
from hopwins.core.task import RecordTask, TaskContext


class Source(Protocol):
    name: str

    def read(self) -> ByteChunk | None: ...

    def close(self) -> None: ...


class Decoder(Protocol):
    name: str

    def feed(self, chunk: ByteChunk) -> list[Record]: ...

    def flush(self) -> list[Record]: ...

    def statistics(self) -> dict[str, object]: ...


def run_pipeline(
    source: Source,
    decoder: Decoder,
    task: RecordTask,
    context: TaskContext,
    *,
    duration_s: float | None = None,
    record_raw: bool = True,
) -> dict[str, object]:
    """Run one task with identical processing for serial and replay sources."""
    if context.recorder is None:
        raise RuntimeError("pipeline context requires a SessionRecorder")
    recorder = context.recorder
    started = monotonic()
    chunks = 0
    records = 0
    task_started = False
    task_result: dict[str, object] = {}
    try:
        task.start(context)
        task_started = True
        while duration_s is None or monotonic() - started < duration_s:
            chunk = source.read()
            if chunk is None:
                break
            if not chunk.data:
                continue
            chunks += 1
            if record_raw:
                recorder.write_chunk(chunk)
            decoded = decoder.feed(chunk)
            records += len(decoded)
            for record in decoded:
                task.handle(record, context)
        for record in decoder.flush():
            records += 1
            task.handle(record, context)
    finally:
        try:
            if task_started:
                task_result = task.stop(context)
        finally:
            source.close()
    result = dict(task_result)
    result.update(
        {
            "source": source.name,
            "decoder": decoder.name,
            "chunks": chunks,
            "records": records,
            "decoder_statistics": decoder.statistics(),
        }
    )
    return result
