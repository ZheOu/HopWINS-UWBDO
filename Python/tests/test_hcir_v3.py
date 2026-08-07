from __future__ import annotations

import unittest

from hopwins.capture.assembler import CirCaptureAssembler
from hopwins.capture.pairing import DualCirPairAssembler
from hopwins.protocol.packets import CirSource, PacketType, parse_hcir_packet
from tests.helpers import build_v3_packet


class HcirV3Tests(unittest.TestCase):
    def test_parser_decodes_pdoa_extension(self) -> None:
        packet = parse_hcir_packet(
            build_v3_packet(
                PacketType.RX_FRAME,
                cir_source=CirSource.STS1,
                rf_port=2,
                pdoa_q1_11=-512,
                tdoa_dtu=-33,
            )
        )

        header = packet.header
        self.assertEqual(header.version, 3)
        self.assertEqual(header.header_length, 176)
        self.assertEqual(header.cir_source, CirSource.STS1)
        self.assertEqual(header.cir_group_size, 2)
        self.assertEqual(header.cir_source_rf_port, 2)
        self.assertAlmostEqual(header.pdoa_radians or 0.0, -0.25)
        self.assertEqual(header.tdoa_dtu, -33)
        self.assertEqual(header.sts0_timestamp, 0x1234500011)
        self.assertEqual(header.sts1_timestamp, 0x1234500022)

    def test_same_capture_id_assembles_and_pairs_both_sources(self) -> None:
        assembler = CirCaptureAssembler()
        pairer = DualCirPairAssembler()
        packets = (
            _packet(PacketType.RX_FRAME, CirSource.STS0, 1, b"frame"),
            _packet(PacketType.RX_FRAME, CirSource.STS1, 2, b"frame"),
            _packet(PacketType.CIR_DATA, CirSource.STS0, 1, _sample(1, 2)),
            _packet(PacketType.CIR_DATA, CirSource.STS1, 2, _sample(3, 4)),
        )

        captures = []
        for packet in packets:
            capture = assembler.add(packet)
            if capture is not None:
                captures.append(capture)

        self.assertEqual(len(captures), 2)
        self.assertEqual(captures[0].header.cir_source, CirSource.STS0)
        self.assertEqual(captures[1].header.cir_source, CirSource.STS1)
        self.assertIsNone(pairer.add(captures[1]))
        pair = pairer.add(captures[0])
        self.assertIsNotNone(pair)
        assert pair is not None
        self.assertEqual(pair.capture_id, 7)
        self.assertEqual(pair.sts0.cir_bytes, _sample(1, 2))
        self.assertEqual(pair.sts1.cir_bytes, _sample(3, 4))


def _packet(
    packet_type: PacketType,
    source: CirSource,
    rf_port: int,
    payload: bytes,
):  # type: ignore[no-untyped-def]
    is_cir = packet_type is PacketType.CIR_DATA
    return parse_hcir_packet(
        build_v3_packet(
            packet_type,
            cir_source=source,
            rf_port=rf_port,
            payload=payload,
            payload_sample_count=1 if is_cir else 0,
            capture_sample_count=1,
        )
    )


def _sample(i_value: int, q_value: int) -> bytes:
    return _i24(i_value) + _i24(q_value)


def _i24(value: int) -> bytes:
    return (value & 0xFFFFFF).to_bytes(3, "little")


if __name__ == "__main__":
    unittest.main()
