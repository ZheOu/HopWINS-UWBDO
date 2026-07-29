from __future__ import annotations

import queue
import unittest

from hopwins.transport.serial_reader import (
    SerialPortInfo,
    SerialWorker,
    WorkerEvent,
    resolve_serial_port,
)

PORTS = [
    SerialPortInfo(
        "COM5",
        "STM32 STLink",
        "USB VID:PID=0483:3754",
        vid=0x0483,
        pid=0x3754,
        serial_number="FOLLOWER",
    ),
    SerialPortInfo(
        "COM6",
        "STM32 STLink",
        "USB VID:PID=0483:3754",
        vid=0x0483,
        pid=0x3754,
        serial_number="LEADER",
    ),
]


class SerialPortSelectionTests(unittest.TestCase):
    def test_explicit_port_does_not_require_discovery(self) -> None:
        self.assertEqual(
            resolve_serial_port("COM9", available_ports=[]),
            "COM9",
        )

    def test_auto_selection_can_use_probe_serial_number(self) -> None:
        self.assertEqual(
            resolve_serial_port(
                "auto",
                vid=0x0483,
                pid=0x3754,
                serial_number="FOLLOWER",
                available_ports=PORTS,
            ),
            "COM5",
        )

    def test_auto_selection_rejects_ambiguous_devices(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "ambiguous"):
            resolve_serial_port(
                "auto",
                vid=0x0483,
                pid=0x3754,
                available_ports=PORTS,
            )

    def test_worker_rejects_wrong_firmware_profile_when_reported(self) -> None:
        events: queue.Queue[WorkerEvent] = queue.Queue()
        worker = SerialWorker(
            "COM5",
            5_000_000,
            events,
            expected_board="Follower-Full",
            expected_role="DO-Follower",
        )
        profile = (
            b"FW PROFILE, BOARD=Leader-UwbOnly, ROLE=DO-Leader, "
            b"BUILD=Debug, FPGA=0, CLOCK=0, EXT_TIMER=0\n"
        )

        with self.assertRaisesRegex(RuntimeError, "profile mismatch"):
            worker._process_bytes(profile)


if __name__ == "__main__":
    unittest.main()
