import unittest

from hopwins.protocol.packets import HcirPacket, PacketType
from hopwins.protocol.stream_parser import HcirStreamParser, TextLine
from tests.helpers import build_v2_packet


class StreamParserTests(unittest.TestCase):
    def test_mixed_text_fragmented_binary_and_v2_fields(self) -> None:
        raw = build_v2_packet(PacketType.RX_FRAME, payload=b"frame")
        parser = HcirStreamParser()
        events = parser.feed(b"FW PROFILE, BOARD=Follower-Full\r\nHC")
        events.extend(parser.feed(b"IR" + raw[4:37]))
        events.extend(parser.feed(raw[37:]))

        self.assertIsInstance(events[0], TextLine)
        packet = next(event for event in events if isinstance(event, HcirPacket))
        self.assertEqual(packet.header.rf_port, 1)
        self.assertEqual(packet.header.mcu_system_time_ms, 1234)
        self.assertEqual(packet.header.reference_time_ms, 1222)
        self.assertEqual(packet.header.first_path_index, 128.5)
        self.assertEqual(packet.payload, b"frame")
        self.assertEqual(parser.statistics.packets, 1)

    def test_crc_error_resynchronizes_to_next_packet(self) -> None:
        damaged = bytearray(build_v2_packet(PacketType.RX_FRAME, payload=b"bad"))
        damaged[30] ^= 0x80
        valid = build_v2_packet(
            PacketType.RX_FRAME,
            capture_id=8,
            payload=b"good",
        )
        parser = HcirStreamParser()
        events = parser.feed(bytes(damaged) + valid)
        packets = [event for event in events if isinstance(event, HcirPacket)]

        self.assertEqual([packet.header.capture_id for packet in packets], [8])
        self.assertEqual(parser.statistics.crc_errors, 1)


if __name__ == "__main__":
    unittest.main()
