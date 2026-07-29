"""Incremental parser for mixed startup text and HCIR binary packets."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from hopwins.protocol.crc import crc32_bzip2
from hopwins.protocol.packets import (
    CRC_LENGTH,
    MAGIC,
    MAX_HEADER_LENGTH,
    MAX_PAYLOAD_LENGTH,
    MIN_HEADER_LENGTH,
    HcirPacket,
    parse_hcir_packet,
)


@dataclass(frozen=True, slots=True)
class TextLine:
    text: str


@dataclass(slots=True)
class ParserStatistics:
    packets: int = 0
    text_lines: int = 0
    crc_errors: int = 0
    framing_errors: int = 0
    discarded_bytes: int = 0


class HcirStreamParser:
    def __init__(self) -> None:
        self._buffer = bytearray()
        self._text_buffer = bytearray()
        self._binary_resync = False
        self.statistics = ParserStatistics()

    def feed(self, data: bytes) -> list[HcirPacket | TextLine]:
        events: list[HcirPacket | TextLine] = []
        if not data:
            return events
        self._buffer.extend(data)

        while self._buffer:
            magic_index = self._buffer.find(MAGIC)
            if magic_index < 0:
                keep = self._partial_magic_suffix_length()
                text_length = len(self._buffer) - keep
                if text_length:
                    if self._binary_resync:
                        self.statistics.discarded_bytes += text_length
                    else:
                        self._consume_text(self._buffer[:text_length], events)
                    del self._buffer[:text_length]
                break

            if magic_index:
                if self._binary_resync:
                    self.statistics.discarded_bytes += magic_index
                else:
                    self._consume_text(self._buffer[:magic_index], events)
                del self._buffer[:magic_index]

            if len(self._buffer) < 12:
                break

            header_length = struct.unpack_from("<H", self._buffer, 6)[0]
            payload_length = struct.unpack_from("<H", self._buffer, 8)[0]
            if not (
                MIN_HEADER_LENGTH <= header_length <= MAX_HEADER_LENGTH
                and payload_length <= MAX_PAYLOAD_LENGTH
            ):
                self.statistics.framing_errors += 1
                self.statistics.discarded_bytes += 1
                self._binary_resync = True
                del self._buffer[0]
                continue

            packet_length = header_length + payload_length + CRC_LENGTH
            if len(self._buffer) < packet_length:
                break

            raw = bytes(self._buffer[:packet_length])
            expected_crc = struct.unpack_from("<I", raw, packet_length - 4)[0]
            actual_crc = crc32_bzip2(raw[:-4])
            if actual_crc != expected_crc:
                self.statistics.crc_errors += 1
                self.statistics.discarded_bytes += 1
                self._binary_resync = True
                del self._buffer[0]
                continue

            try:
                packet = parse_hcir_packet(raw)
            except ValueError:
                self.statistics.framing_errors += 1
                self.statistics.discarded_bytes += 1
                self._binary_resync = True
                del self._buffer[0]
                continue

            del self._buffer[:packet_length]
            self._binary_resync = False
            self.statistics.packets += 1
            events.append(packet)

        return events

    def flush_text(self) -> list[TextLine]:
        events: list[HcirPacket | TextLine] = []
        if self._buffer:
            self._consume_text(self._buffer, events)
            self._buffer.clear()
        if self._text_buffer:
            self._emit_text_line(bytes(self._text_buffer), events)
            self._text_buffer.clear()
        return [event for event in events if isinstance(event, TextLine)]

    def _partial_magic_suffix_length(self) -> int:
        maximum = min(len(self._buffer), len(MAGIC) - 1)
        for length in range(maximum, 0, -1):
            if self._buffer[-length:] == MAGIC[:length]:
                return length
        return 0

    def _consume_text(
        self,
        data: bytes | bytearray,
        events: list[HcirPacket | TextLine],
    ) -> None:
        self._text_buffer.extend(data)
        if b"\n" not in self._text_buffer and len(self._text_buffer) > 4096:
            self.statistics.discarded_bytes += len(self._text_buffer)
            self._text_buffer.clear()
            return
        while True:
            newline = self._text_buffer.find(b"\n")
            if newline < 0:
                break
            line = bytes(self._text_buffer[:newline]).rstrip(b"\r")
            del self._text_buffer[: newline + 1]
            self._emit_text_line(line, events)

    def _emit_text_line(
        self,
        line: bytes,
        events: list[HcirPacket | TextLine],
    ) -> None:
        if not line:
            return
        printable = sum(value in (9, 13) or 32 <= value < 127 for value in line)
        if printable * 5 < len(line) * 4:
            self.statistics.discarded_bytes += len(line)
            return
        event = TextLine(line.decode("ascii", errors="replace"))
        self.statistics.text_lines += 1
        events.append(event)
