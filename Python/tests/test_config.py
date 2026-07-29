from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from hopwins.config import ConfigurationError, ProjectConfig
from hopwins.tasks.registry import TASKS

BASE_CONFIG = """\
[app]
task = cir_monitor
device = follower

[device.follower]
port = auto
baudrate = 5000000
timeout_s = 0.1
vid = 0x0483
pid = 0x3754
expected_board = Follower-Full
expected_role = DO-Follower

[storage]
capture_directory = captures
filename_template = {timestamp}_{device}_{task}.hcir

[task.cir_monitor]
record = true
rolling_window = 200
"""


class ProjectConfigTests(unittest.TestCase):
    def test_local_file_overrides_shared_device_settings(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "config.ini"
            path.write_text(BASE_CONFIG, encoding="utf-8")
            (root / "config.local.ini").write_text(
                "[app]\ntask = raw_record\n"
                "[device.follower]\nport = COM7\nbaudrate = 3000000\n",
                encoding="utf-8",
            )

            config = ProjectConfig.load(path)
            device = config.device()

            self.assertEqual(device.port, "COM7")
            self.assertEqual(device.baudrate, 3_000_000)
            self.assertEqual(config.task_name, "raw_record")
            self.assertEqual(len(config.loaded_paths), 2)

    def test_capture_name_is_created_below_configured_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "config.ini"
            path.write_text(BASE_CONFIG, encoding="utf-8")
            config = ProjectConfig.load(path)

            capture = config.new_capture_path("cir_monitor", "follower")

            self.assertEqual(capture.parent, (root / "captures").resolve())
            self.assertTrue(capture.name.endswith("_follower_cir_monitor.hcir"))

    def test_unknown_device_section_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.ini"
            path.write_text(BASE_CONFIG, encoding="utf-8")
            config = ProjectConfig.load(path)

            with self.assertRaises(ConfigurationError):
                config.device("missing")

    def test_registry_uses_stable_explicit_task_names(self) -> None:
        self.assertEqual(
            tuple(TASKS),
            ("cir_monitor", "raw_record", "replay", "list_ports"),
        )


if __name__ == "__main__":
    unittest.main()
