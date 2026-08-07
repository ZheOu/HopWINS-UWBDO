from __future__ import annotations

import os
import queue
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6 import QtWidgets

from hopwins.capture.assembler import CirCaptureAssembler
from hopwins.io.workers import CaptureEvent, WorkerEvent
from hopwins.protocol.packets import CirSource, PacketType, parse_hcir_packet
from hopwins.protocol.stream_parser import HcirStreamParser
from hopwins.ui.cir_window import CirMonitorWindow
from hopwins.ui.dual_cir_window import DualCirMonitorWindow
from tests.helpers import build_v2_packet, build_v3_packet


class _FakeWorker:
    def __init__(self) -> None:
        self.parser = HcirStreamParser()
        self.assembler = CirCaptureAssembler()

    def start(self) -> None:
        pass

    def stop(self) -> None:
        pass


class UiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.application = QtWidgets.QApplication.instance()
        if cls.application is None:
            cls.application = QtWidgets.QApplication([])

    def test_complete_capture_updates_plot_and_metadata(self) -> None:
        frame = parse_hcir_packet(
            build_v2_packet(
                PacketType.RX_FRAME,
                payload=b"frame",
                capture_sample_count=1,
            )
        )
        samples = parse_hcir_packet(
            build_v2_packet(
                PacketType.CIR_DATA,
                payload=b"\x01\x00\x00\x02\x00\x00",
                payload_sample_count=1,
                capture_sample_count=1,
            )
        )
        assembler = CirCaptureAssembler()
        self.assertIsNone(assembler.add(frame))
        capture = assembler.add(samples)
        assert capture is not None

        events: queue.Queue[WorkerEvent] = queue.Queue()
        events.put(CaptureEvent(capture))
        worker = _FakeWorker()
        window = CirMonitorWindow(worker, events, 20, "test")  # type: ignore[arg-type]
        window._drain_events()

        self.assertTrue(window._labels["rf_port"].text().startswith("1"))
        x_data, _ = window._i_curve.getData()
        self.assertEqual(len(x_data), 1)
        window.close()

    def test_dual_monitor_pairs_and_plots_sts0_and_sts1(self) -> None:
        assembler = CirCaptureAssembler()
        captures = []
        for source, rf_port, sample in (
            (CirSource.STS0, 1, b"\x01\x00\x00\x02\x00\x00"),
            (CirSource.STS1, 2, b"\x03\x00\x00\x04\x00\x00"),
        ):
            frame = parse_hcir_packet(
                build_v3_packet(
                    PacketType.RX_FRAME,
                    cir_source=source,
                    rf_port=rf_port,
                    payload=b"frame",
                    capture_sample_count=1,
                )
            )
            chunk = parse_hcir_packet(
                build_v3_packet(
                    PacketType.CIR_DATA,
                    cir_source=source,
                    rf_port=rf_port,
                    payload=sample,
                    payload_sample_count=1,
                    capture_sample_count=1,
                )
            )
            self.assertIsNone(assembler.add(frame))
            capture = assembler.add(chunk)
            assert capture is not None
            captures.append(capture)

        events: queue.Queue[WorkerEvent] = queue.Queue()
        for capture in captures:
            events.put(CaptureEvent(capture))
        worker = _FakeWorker()
        window = DualCirMonitorWindow(
            worker,  # type: ignore[arg-type]
            events,
            "test",
        )
        window._drain_events()

        self.assertEqual(window._labels["capture"].text(), "7")
        self.assertIn("STS0/RF1", window._labels["channels"].text())
        x0, _ = window._sts0_magnitude.getData()
        x1, _ = window._sts1_magnitude.getData()
        self.assertEqual(len(x0), 1)
        self.assertEqual(len(x1), 1)
        window.close()


if __name__ == "__main__":
    unittest.main()
