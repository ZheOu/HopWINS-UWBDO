"""Transport-neutral byte chunks and decoded records."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import UTC, datetime
from time import monotonic_ns, time_ns


@dataclass(frozen=True, slots=True)
class ByteChunk:
    data: bytes
    host_monotonic_ns: int
    host_utc_ns: int
    source: str

    @classmethod
    def now(cls, data: bytes, source: str) -> ByteChunk:
        return cls(data, monotonic_ns(), time_ns(), source)

    @property
    def host_utc(self) -> str:
        return datetime.fromtimestamp(
            self.host_utc_ns / 1_000_000_000,
            tz=UTC,
        ).isoformat()


@dataclass(frozen=True, slots=True)
class Record:
    kind: str
    schema: str
    payload: object
    host_monotonic_ns: int
    host_utc_ns: int
