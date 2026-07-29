"""Assemble one RX frame and its CIR chunks into an immutable capture."""

from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass, field

from hopwins.protocol.packets import HcirHeader, HcirPacket, PacketFlags, PacketType


@dataclass(frozen=True, slots=True)
class CirCapture:
    header: HcirHeader
    frame: bytes
    cir_bytes: bytes
    received_chunks: int
    expected_chunks: int

    @property
    def has_cir(self) -> bool:
        return bool(self.cir_bytes) and (
            self.header.version == 1
            or bool(self.header.flags & PacketFlags.CIR_VALID)
        )


@dataclass(slots=True)
class AssemblerStatistics:
    completed_captures: int = 0
    incomplete_captures: int = 0
    duplicate_chunks: int = 0
    invalid_chunks: int = 0


@dataclass(slots=True)
class _PendingCapture:
    frame_packet: HcirPacket | None = None
    first_header: HcirHeader | None = None
    expected_chunks: int = 0
    chunks: dict[int, HcirPacket] = field(default_factory=dict)


class CirCaptureAssembler:
    def __init__(self, maximum_pending: int = 8) -> None:
        self._pending: OrderedDict[int, _PendingCapture] = OrderedDict()
        self._maximum_pending = maximum_pending
        self.statistics = AssemblerStatistics()

    def add(self, packet: HcirPacket) -> CirCapture | None:
        capture_id = packet.header.capture_id
        pending = self._pending.setdefault(capture_id, _PendingCapture())
        pending.first_header = pending.first_header or packet.header
        self._pending.move_to_end(capture_id)

        if packet.header.packet_type is PacketType.RX_FRAME:
            pending.frame_packet = packet
            if packet.header.version >= 2 and not (
                packet.header.flags & PacketFlags.CIR_VALID
            ):
                return self._finish_without_cir(capture_id, pending)
        elif packet.header.packet_type is PacketType.CIR_DATA:
            if not self._add_chunk(pending, packet):
                return None

        capture = self._try_finish(capture_id, pending)
        self._evict_old_captures()
        return capture

    def _add_chunk(self, pending: _PendingCapture, packet: HcirPacket) -> bool:
        header = packet.header
        if (
            header.chunk_count == 0
            or header.chunk_index >= header.chunk_count
            or header.bytes_per_sample == 0
            or len(packet.payload)
            != header.payload_sample_count * header.bytes_per_sample
        ):
            self.statistics.invalid_chunks += 1
            return False
        if header.chunk_index in pending.chunks:
            self.statistics.duplicate_chunks += 1
            return False
        if pending.expected_chunks not in (0, header.chunk_count):
            self.statistics.invalid_chunks += 1
            return False

        pending.expected_chunks = header.chunk_count
        pending.chunks[header.chunk_index] = packet
        return True

    def _try_finish(
        self,
        capture_id: int,
        pending: _PendingCapture,
    ) -> CirCapture | None:
        if pending.frame_packet is None or pending.expected_chunks == 0:
            return None
        if len(pending.chunks) != pending.expected_chunks:
            return None

        header = pending.frame_packet.header
        sample_bytes = header.bytes_per_sample
        output = bytearray(header.capture_sample_count * sample_bytes)
        coverage = bytearray(header.capture_sample_count)

        for packet in pending.chunks.values():
            chunk = packet.header
            relative_sample = (
                chunk.payload_sample_offset - header.capture_sample_offset
            )
            end_sample = relative_sample + chunk.payload_sample_count
            if (
                relative_sample < 0
                or end_sample > header.capture_sample_count
                or chunk.bytes_per_sample != sample_bytes
            ):
                self.statistics.invalid_chunks += 1
                return None
            byte_start = relative_sample * sample_bytes
            output[byte_start : byte_start + len(packet.payload)] = packet.payload
            coverage[relative_sample:end_sample] = b"\x01" * chunk.payload_sample_count

        if not all(coverage):
            return None

        del self._pending[capture_id]
        self.statistics.completed_captures += 1
        return CirCapture(
            header=header,
            frame=pending.frame_packet.payload,
            cir_bytes=bytes(output),
            received_chunks=len(pending.chunks),
            expected_chunks=pending.expected_chunks,
        )

    def _finish_without_cir(
        self,
        capture_id: int,
        pending: _PendingCapture,
    ) -> CirCapture:
        assert pending.frame_packet is not None
        del self._pending[capture_id]
        self.statistics.completed_captures += 1
        return CirCapture(
            header=pending.frame_packet.header,
            frame=pending.frame_packet.payload,
            cir_bytes=b"",
            received_chunks=0,
            expected_chunks=0,
        )

    def _evict_old_captures(self) -> None:
        while len(self._pending) > self._maximum_pending:
            self._pending.popitem(last=False)
            self.statistics.incomplete_captures += 1
