from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from hopwins.config import ConfigurationError, ProjectConfig
from hopwins.registry import TASKS

BASE_CONFIG = """\
[app]
task = "session_capture"
device = "follower"

[devices.follower]
port = "auto"
baudrate = 5000000
timeout_s = 0.1
vid = 0x0483
pid = 0x3754
expected_board = "UWB-RF1-SiT5156"
expected_role = "DO-Follower"

[storage]
experiment_directory = "experiments"
capture_directory = "captures"
filename_template = "{timestamp}_{device}_{task}.hcir"

[tasks.session_capture]
protocol = "hcir_v2"
duration_s = 30
"""


class ProjectConfigTests(unittest.TestCase):
    def test_local_file_deeply_overrides_shared_settings(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "config.toml"
            path.write_text(BASE_CONFIG, encoding="utf-8")
            (root / "config.local.toml").write_text(
                "[app]\ntask = \"raw_record\"\n"
                "[devices.follower]\nport = \"COM7\"\nbaudrate = 3000000\n",
                encoding="utf-8",
            )

            config = ProjectConfig.load(path)
            device = config.device()

            self.assertEqual(device.port, "COM7")
            self.assertEqual(device.baudrate, 3_000_000)
            self.assertEqual(device.expected_role, "DO-Follower")
            self.assertEqual(config.task_name, "raw_record")
            self.assertEqual(len(config.loaded_paths), 2)

    def test_session_name_and_task_overrides_are_stable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.toml"
            path.write_text(BASE_CONFIG, encoding="utf-8")
            config = ProjectConfig.load(path)

            session = config.new_session_path(
                "session_capture",
                "follower",
                "corridor LOS 5m",
            )
            parameters = config.task_parameters(
                "session_capture",
                {"duration_s": 60},
            )

            self.assertEqual(session.parent, (path.parent / "experiments").resolve())
            self.assertTrue(session.name.endswith("corridor-LOS-5m"))
            self.assertEqual(parameters["duration_s"], 60)
            self.assertEqual(parameters["protocol"], "hcir_v2")

    def test_unknown_device_table_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.toml"
            path.write_text(BASE_CONFIG, encoding="utf-8")
            config = ProjectConfig.load(path)

            with self.assertRaises(ConfigurationError):
                config.device("missing")

    def test_registry_exposes_category_and_mode_metadata(self) -> None:
        capture = TASKS["session_capture"]

        self.assertEqual(capture.category, "capture")
        self.assertTrue(capture.supports_online)
        self.assertTrue(capture.supports_offline)
        self.assertEqual(capture.protocol, "hcir")
        dual = TASKS["dual_cir_monitor"]
        self.assertEqual(dual.category, "diagnostic")
        self.assertTrue(dual.supports_online)
        self.assertTrue(dual.supports_offline)
        self.assertEqual(dual.protocol, "hcir_v3")

    def test_latest_capture_ignores_empty_derived_streams(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "config.toml"
            path.write_text(BASE_CONFIG, encoding="utf-8")
            capture_path = root / "captures" / "recording.hcir"
            capture_path.parent.mkdir()
            capture_path.write_bytes(b"recorded data")
            derived_path = root / "experiments" / "derived" / "raw" / "serial.bin"
            derived_path.parent.mkdir(parents=True)
            derived_path.write_bytes(b"")

            latest = ProjectConfig.load(path).latest_capture_path()

        self.assertEqual(latest, capture_path.resolve())


if __name__ == "__main__":
    unittest.main()
