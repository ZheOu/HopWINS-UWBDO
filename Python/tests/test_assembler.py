import unittest

from hopwins.capture.assembler import CirCaptureAssembler
from hopwins.protocol.packets import PacketType, parse_hcir_packet
from tests.helpers import build_v2_packet


class AssemblerTests(unittest.TestCase):
    def test_out_of_order_chunks_are_placed_by_sample_offset(self) -> None:
        frame = parse_hcir_packet(
            build_v2_packet(
                PacketType.RX_FRAME,
                payload=b"uwb-frame",
                capture_sample_count=3,
            )
        )
        second = parse_hcir_packet(
            build_v2_packet(
                PacketType.CIR_DATA,
                chunk_index=1,
                chunk_count=2,
                payload=b"C" * 6,
                payload_sample_offset=102,
                payload_sample_count=1,
                capture_sample_count=3,
            )
        )
        first = parse_hcir_packet(
            build_v2_packet(
                PacketType.CIR_DATA,
                chunk_index=0,
                chunk_count=2,
                payload=b"A" * 6 + b"B" * 6,
                payload_sample_offset=100,
                payload_sample_count=2,
                capture_sample_count=3,
            )
        )

        assembler = CirCaptureAssembler()
        self.assertIsNone(assembler.add(frame))
        self.assertIsNone(assembler.add(second))
        capture = assembler.add(first)

        self.assertIsNotNone(capture)
        assert capture is not None
        self.assertEqual(capture.frame, b"uwb-frame")
        self.assertEqual(capture.cir_bytes, b"A" * 6 + b"B" * 6 + b"C" * 6)
        self.assertEqual(assembler.statistics.completed_captures, 1)


if __name__ == "__main__":
    unittest.main()
