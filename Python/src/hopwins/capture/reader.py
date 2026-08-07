"""Synchronous iteration over captures in a recorded mixed HCIR stream."""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

from hopwins.capture.assembler import CirCapture, CirCaptureAssembler
from hopwins.protocol.packets import HcirPacket
from hopwins.protocol.stream_parser import HcirStreamParser
from hopwins.storage.reader import resolve_raw_stream


class CaptureFileReader:
    def __init__(
        self,
        path: str | Path,
        *,
        block_size: int = 8192,
    ) -> None:
        self.path = resolve_raw_stream(path)
        self.block_size = block_size
        self.parser = HcirStreamParser()
        self.assembler = CirCaptureAssembler()

    def __iter__(self) -> Iterator[CirCapture]:
        with self.path.open("rb") as stream:
            while data := stream.read(self.block_size):
                for event in self.parser.feed(data):
                    if not isinstance(event, HcirPacket):
                        continue
                    capture = self.assembler.add(event)
                    if capture is not None:
                        yield capture
