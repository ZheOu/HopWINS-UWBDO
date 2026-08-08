"""Mixed text and versioned HCIR decoder for online/offline pipelines."""

from __future__ import annotations

from dataclasses import asdict

from hopwins.capture.assembler import CirCaptureAssembler
from hopwins.core.records import ByteChunk, Record
from hopwins.protocol.common_text import FirmwareProfile, parse_firmware_profile
from hopwins.protocol.do_text_v1 import (
    DoTrackConfig,
    DoTrackRecord,
    parse_do_track,
    parse_do_track_config,
)
from hopwins.protocol.packets import HcirPacket
from hopwins.protocol.service_text_v1 import (
    UwbRxHealthRecord,
    UwbTxRecord,
    parse_uwb_rx_health,
    parse_uwb_tx,
)
from hopwins.protocol.stream_parser import HcirStreamParser, TextLine


class HcirDecoder:
    name = "hcir_v2_v3"

    def __init__(self) -> None:
        self.parser = HcirStreamParser()
        self.assembler = CirCaptureAssembler()
        self._special_text_records = 0

    def feed(self, chunk: ByteChunk) -> list[Record]:
        records: list[Record] = []
        for event in self.parser.feed(chunk.data):
            records.extend(self._convert(event, chunk))
        return records

    def flush(self) -> list[Record]:
        chunk = ByteChunk.now(b"", "decoder-flush")
        records: list[Record] = []
        for event in self.parser.flush_text():
            records.extend(self._convert(event, chunk))
        return records

    def statistics(self) -> dict[str, object]:
        return {
            "parser": asdict(self.parser.statistics),
            "assembler": asdict(self.assembler.statistics),
            "special_text_records": self._special_text_records,
        }

    def _convert(
        self,
        event: HcirPacket | TextLine,
        chunk: ByteChunk,
    ) -> list[Record]:
        common = {
            "host_monotonic_ns": chunk.host_monotonic_ns,
            "host_utc_ns": chunk.host_utc_ns,
        }
        if isinstance(event, TextLine):
            records = [Record("text.line", "text.v1", event.text, **common)]
            parsed = (
                parse_firmware_profile(event.text)
                or parse_do_track_config(event.text)
                or parse_do_track(event.text)
                or parse_uwb_tx(event.text)
                or parse_uwb_rx_health(event.text)
            )
            if parsed is not None:
                self._special_text_records += 1
                if isinstance(parsed, FirmwareProfile):
                    kind, schema = "firmware.profile", "firmware_profile.v1"
                elif isinstance(parsed, DoTrackConfig):
                    kind, schema = "do.track.config", "do_text.v1"
                elif isinstance(parsed, DoTrackRecord):
                    kind, schema = "do.track", "do_text.v1"
                elif isinstance(parsed, UwbTxRecord):
                    kind, schema = "uwb.tx", "service_text.v1"
                elif isinstance(parsed, UwbRxHealthRecord):
                    kind, schema = "uwb.rx_health", "service_text.v1"
                else:  # pragma: no cover - guards future parser additions
                    raise TypeError(f"unsupported text record: {type(parsed).__name__}")
                records.append(Record(kind, schema, parsed, **common))
            return records

        schema = f"hcir.v{event.header.version}"
        records = [Record("hcir.packet", schema, event, **common)]
        capture = self.assembler.add(event)
        if capture is not None:
            records.append(Record("cir.capture", schema, capture, **common))
        return records
