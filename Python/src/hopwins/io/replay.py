"""Offline byte source for a legacy capture or a recorded session."""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import BinaryIO

from hopwins.core.records import ByteChunk
from hopwins.storage.reader import resolve_raw_stream


class ReplaySource:
    def __init__(
        self,
        path: str | Path,
        *,
        block_size: int = 8192,
        speed: float = 0.0,
    ) -> None:
        self.path = resolve_raw_stream(path)
        self.name = f"replay:{self.path}"
        self._stream: BinaryIO = self.path.open("rb")
        self._block_size = block_size
        self._speed = max(speed, 0.0)
        self._index = self._load_index()
        self._index_position = 0
        self._previous_time_ns: int | None = None

    def read(self) -> ByteChunk | None:
        if self._index_position < len(self._index):
            entry = self._index[self._index_position]
            self._index_position += 1
            data = self._stream.read(int(entry["length"]))
            timestamp = int(entry["host_monotonic_ns"])
            utc_ns = int(entry.get("host_utc_ns", 0))
            self._delay(timestamp)
            return ByteChunk(data, timestamp, utc_ns, self.name)
        data = self._stream.read(self._block_size)
        if not data:
            return None
        return ByteChunk.now(data, self.name)

    def close(self) -> None:
        self._stream.close()

    def _load_index(self) -> list[dict[str, int]]:
        index_path = self.path.with_name("serial.index.jsonl")
        if not index_path.is_file():
            return []
        entries = []
        for line in index_path.read_text(encoding="utf-8").splitlines():
            value = json.loads(line)
            if isinstance(value, dict):
                entries.append(value)
        return entries

    def _delay(self, timestamp_ns: int) -> None:
        if self._speed > 0 and self._previous_time_ns is not None:
            delay = (timestamp_ns - self._previous_time_ns) / 1_000_000_000
            time.sleep(max(0.0, min(delay / self._speed, 1.0)))
        self._previous_time_ns = timestamp_ns
