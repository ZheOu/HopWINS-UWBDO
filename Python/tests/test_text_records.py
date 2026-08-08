from __future__ import annotations

import unittest

from hopwins.core.records import ByteChunk
from hopwins.protocol.common_text import parse_firmware_profile
from hopwins.protocol.do_text_v1 import parse_do_track, parse_do_track_config
from hopwins.protocol.hcir_v2 import HcirV2Decoder
from hopwins.protocol.service_text_v1 import parse_uwb_rx_health, parse_uwb_tx


class TextRecordTests(unittest.TestCase):
    def test_new_firmware_profile_is_typed(self) -> None:
        profile = parse_firmware_profile(
            "FW PROFILE, BOARD=UWB-RF1-SiT5156, ROLE=DO-Follower, "
            "BUILD=Debug, FPGA=0, CLOCK=1, EXT_TIMER=1, CRC32=0x12345678"
        )

        self.assertIsNotNone(profile)
        assert profile is not None
        self.assertEqual(profile.board, "UWB-RF1-SiT5156")
        self.assertTrue(profile.has_clock_control)

    def test_current_firmware_profile_names_clock_and_rf_paths(self) -> None:
        profile = parse_firmware_profile(
            "FW PROFILE, BOARD=Full-SiT5156, "
            "ROLE=UWB-STS-Dual-RX-Diagnostic, BUILD=Debug, FPGA=1, "
            "CLOCK_DEVICE=SiT5156, EXT_TIMER=1, RF_PATHS=0x3"
        )

        self.assertIsNotNone(profile)
        assert profile is not None
        self.assertEqual(profile.clock_device, "SiT5156")
        self.assertEqual(profile.rf_paths, 3)
        self.assertTrue(profile.has_clock_control)

    def test_do_tracking_config_and_update_are_typed(self) -> None:
        config = parse_do_track_config(
            "DO TRACK CFG, ENABLE=1, READY=1, LOOP=ENDPOINT_SLOPE, "
            "TIMESTAMP=DW_ADJUSTED, WINDOW=20, MAX_ERR_PPB=200000, "
            "CMD_LIMIT_PPB=200000, CAPTURE_OUTPUT=OFF, CIR_CAPTURE=0"
        )
        update = parse_do_track(
            "DO TRACK, RESULT=UPDATE, SEQ0=0x00000001, SEQ1=0x00000015, "
            "N=20, TX_DT=0x0001000000, RX_DT=0x0001000100, "
            "ERR_PPB=125, CMD_PPB=-125, CLK_STATUS=0x00, OBS=1, "
            "UPDATES=0x00000001, REJECT=0x00000000"
        )

        self.assertIsNotNone(config)
        self.assertIsNotNone(update)
        assert config is not None and update is not None
        self.assertEqual(config.window, 20)
        self.assertEqual(update.error_ppb, 125)
        self.assertEqual(update.command_ppb, -125)

    def test_mixed_decoder_emits_raw_text_and_special_record(self) -> None:
        decoder = HcirV2Decoder()
        line = (
            b"DO TRACK, RESULT=OUTLIER, SEQ0=0x1, SEQ1=0x15, N=20, "
            b"TX_DT=0x100, RX_DT=0x200, ERR_PPB=OUT_OF_RANGE, "
            b"CMD_PPB=0, CLK_STATUS=0x00, OBS=0, UPDATES=0x0, REJECT=0x1\n"
        )
        records = decoder.feed(ByteChunk.now(line, "test"))

        self.assertEqual([record.kind for record in records], ["text.line", "do.track"])

    def test_uwb_tx_and_rx_health_records_are_typed(self) -> None:
        transmit = parse_uwb_tx(
            "UWB TX OK, STATUS=0x00, SEQ=0x0000002A, LEN=0x0017, "
            "SCHED=0x00123456, TX_TS=0x123456789A, LATE=0x00000001"
        )
        health = parse_uwb_rx_health(
            "UWB RX HEALTH, ENABLE=1, PENDING=1, QUEUED=0x02, "
            "RX=0x0000002A, ERR=0x00000001, CRC_ERR=0x00000000, "
            "RECOVERY=0x00000002, WATCHDOG=0x00000003, QFULL=0x00000004, "
            "UART_ERR=0x00000005"
        )

        self.assertIsNotNone(transmit)
        self.assertIsNotNone(health)
        assert transmit is not None and health is not None
        self.assertEqual(transmit.sequence, 42)
        self.assertEqual(transmit.transmit_timestamp, 0x123456789A)
        self.assertEqual(health.received_count, 42)
        self.assertEqual(health.queue_full_count, 4)


if __name__ == "__main__":
    unittest.main()
