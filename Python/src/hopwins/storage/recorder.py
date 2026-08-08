"""Generic session recorder plus the legacy single-file recorder."""

from __future__ import annotations

import csv
import json
from collections.abc import Mapping, Sequence
from dataclasses import asdict, is_dataclass
from datetime import UTC, datetime
from enum import Enum
from pathlib import Path
from typing import BinaryIO, TextIO

from hopwins.core.records import ByteChunk
from hopwins.core.session import ExperimentSession
from hopwins.protocol.common_text import FirmwareProfile


class SessionRecorder:
    def __init__(
        self,
        session: ExperimentSession,
        *,
        flush_every_chunks: int = 32,
    ) -> None:
        if flush_every_chunks <= 0:
            raise ValueError("flush_every_chunks must be positive")
        self.session = session
        self.flush_every_chunks = flush_every_chunks
        self._raw: BinaryIO = (session.path / "raw" / "serial.bin").open("wb")
        self._index = (session.path / "raw" / "serial.index.jsonl").open(
            "w", encoding="utf-8", newline="\n"
        )
        self._events = (session.path / "records" / "events.jsonl").open(
            "w", encoding="utf-8", newline="\n"
        )
        self._csv_files: dict[str, TextIO] = {}
        self._csv_writers: dict[str, csv.DictWriter[str]] = {}
        self._csv_fields: dict[str, tuple[str, ...]] = {}
        self.bytes_written = 0
        self.chunks_written = 0

    def write_chunk(self, chunk: ByteChunk) -> None:
        offset = self.bytes_written
        self._raw.write(chunk.data)
        self.bytes_written += len(chunk.data)
        self.chunks_written += 1
        self._index.write(
            json.dumps(
                {
                    "offset": offset,
                    "length": len(chunk.data),
                    "host_monotonic_ns": chunk.host_monotonic_ns,
                    "host_utc_ns": chunk.host_utc_ns,
                    "source": chunk.source,
                },
                separators=(",", ":"),
            )
            + "\n"
        )
        if self.chunks_written % self.flush_every_chunks == 0:
            self.flush()

    def write_event(self, kind: str, **values: object) -> None:
        event = {
            "kind": kind,
            "host_utc": datetime.now(UTC).isoformat(),
            **values,
        }
        self._events.write(json.dumps(_jsonable(event), separators=(",", ":")) + "\n")

    def write_row(
        self,
        name: str,
        fields: Sequence[str],
        row: Mapping[str, object],
    ) -> None:
        self.ensure_table(name, fields)
        writer = self._csv_writers[name]
        normalized_fields = self._csv_fields[name]
        writer.writerow({field: row.get(field) for field in normalized_fields})

    def ensure_table(self, name: str, fields: Sequence[str]) -> None:
        """Create a CSV and its header even when no data rows arrive."""
        normalized_fields = tuple(fields)
        writer = self._csv_writers.get(name)
        if writer is None:
            path = self.session.path / "records" / f"{name}.csv"
            stream = path.open("w", encoding="utf-8", newline="")
            writer = csv.DictWriter(stream, fieldnames=normalized_fields)
            writer.writeheader()
            self._csv_files[name] = stream
            self._csv_writers[name] = writer
            self._csv_fields[name] = normalized_fields
        elif self._csv_fields[name] != normalized_fields:
            raise ValueError(f"CSV schema changed during session: {name}")

    def close(self) -> None:
        if self._raw.closed:
            return
        self.flush()
        self._raw.close()
        self._index.close()
        self._events.close()
        for stream in self._csv_files.values():
            stream.close()

    def flush(self) -> None:
        if self._raw.closed:
            return
        self._raw.flush()
        self._index.flush()
        self._events.flush()
        for stream in self._csv_files.values():
            stream.flush()


class SessionRawSink:
    """Adapt a SessionRecorder to the lossless sink used by serial workers."""

    def __init__(
        self,
        recorder: SessionRecorder,
        session: ExperimentSession,
        source: str,
    ) -> None:
        self.recorder = recorder
        self.session = session
        self.source = source

    def write(self, data: bytes) -> None:
        self.recorder.write_chunk(ByteChunk.now(data, self.source))

    def set_profile(self, profile: FirmwareProfile) -> None:
        self.session.update(firmware_profile=asdict(profile))
        self.recorder.write_event("firmware.profile", profile=asdict(profile))


class RawSessionRecorder:
    """Compatibility recorder for legacy tasks that request one .hcir file."""

    def __init__(
        self,
        path: str | Path,
        session_metadata: Mapping[str, object] | None = None,
    ) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file: BinaryIO = self.path.open("wb")
        self._started = datetime.now(UTC)
        self._bytes_written = 0
        self._profile: FirmwareProfile | None = None
        self._session_metadata = dict(session_metadata or {})

    def write(self, data: bytes) -> None:
        self._file.write(data)
        self._bytes_written += len(data)

    def set_profile(self, profile: FirmwareProfile) -> None:
        self._profile = profile

    def close(self) -> None:
        if self._file.closed:
            return
        self._file.flush()
        self._file.close()
        metadata: dict[str, object] = {
            "format": "raw-hcir-stream",
            "started_utc": self._started.isoformat(),
            "finished_utc": datetime.now(UTC).isoformat(),
            "bytes": self._bytes_written,
        }
        if self._session_metadata:
            metadata["session"] = self._session_metadata
        if self._profile is not None:
            metadata["firmware_profile"] = asdict(self._profile)
        self.path.with_suffix(self.path.suffix + ".json").write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def __enter__(self) -> RawSessionRecorder:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def _jsonable(value: object) -> object:
    if is_dataclass(value) and not isinstance(value, type):
        return _jsonable(asdict(value))
    if isinstance(value, dict):
        return {str(key): _jsonable(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_jsonable(item) for item in value]
    if isinstance(value, Enum):
        return value.value
    if isinstance(value, bytes):
        return value.hex()
    if isinstance(value, Path):
        return str(value)
    return value
