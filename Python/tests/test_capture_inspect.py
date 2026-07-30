from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from hopwins.capture.reader import CaptureFileReader
from hopwins.protocol.packets import PacketFlags, PacketType
from hopwins.tasks.capture_inspect import format_capture
from tests.helpers import build_v2_packet


class CaptureInspectTests(unittest.TestCase):
    def test_reader_and_formatter_show_fpi_cir_and_timestamp_semantics(
        self,
    ) -> None:
        samples = [(0, 0)] * 40
        samples[29] = (100, -50)
        samples[33] = (1000, 500)
        cir_payload = b"".join(
            _pack_i24(i_value) + _pack_i24(q_value) for i_value, q_value in samples
        )
        frame = build_v2_packet(
            PacketType.RX_FRAME,
            payload=b"frame",
            capture_sample_offset=100,
            capture_sample_count=40,
        )
        cir = build_v2_packet(
            PacketType.CIR_DATA,
            payload=cir_payload,
            capture_sample_offset=100,
            payload_sample_offset=100,
            payload_sample_count=40,
            capture_sample_count=40,
        )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.hcir"
            path.write_bytes(frame + cir)
            captures = list(CaptureFileReader(path))

        self.assertEqual(len(captures), 1)
        text = format_capture(captures[0], cir_radius=2, frame_bytes=8)
        self.assertIn("RX_STAMP adjusted = 0x123456789A", text)
        self.assertIn("RX_RAWST = not captured", text)
        self.assertIn("FPI=128.500000", text)
        self.assertIn("register_peak=133", text)
        self.assertIn("observed_max=133", text)
        self.assertIn("marks: F=nearest FPI", text)

    def test_formatter_calculates_raw_to_adjusted_cia_correction(self) -> None:
        frame = build_v2_packet(
            PacketType.RX_FRAME,
            payload=b"frame",
            flags=(
                PacketFlags.RX_CRC_GOOD
                | PacketFlags.DIAGNOSTIC_OK
                | PacketFlags.REGISTERS_OK
            ),
            capture_sample_count=0,
            raw_rx_timestamp=0x1234560000,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.hcir"
            path.write_bytes(frame)
            captures = list(CaptureFileReader(path))

        self.assertEqual(captures[0].header.cia_correction_dtu, 47_290)
        self.assertEqual(captures[0].header.cia_fpi_offset_dtu, 39_066)
        text = format_capture(captures[0], cir_radius=0, frame_bytes=0)
        self.assertIn("RX_RAWST coarse = 0x1234560000", text)
        self.assertIn("C_CIA=signed40", text)
        self.assertIn("C_CIA-FPI_Q10.6", text)


def _pack_i24(value: int) -> bytes:
    return (value & 0xFFFFFF).to_bytes(3, "little")


if __name__ == "__main__":
    unittest.main()
