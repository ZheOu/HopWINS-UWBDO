"""Protocol-decoder contract used by the shared pipeline."""

from __future__ import annotations

from typing import Protocol

from hopwins.core.records import ByteChunk, Record


class ProtocolDecoder(Protocol):
    name: str

    def feed(self, chunk: ByteChunk) -> list[Record]: ...

    def flush(self) -> list[Record]: ...

    def statistics(self) -> dict[str, object]: ...
