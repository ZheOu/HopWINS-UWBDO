from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from hopwins.core.records import ByteChunk
from hopwins.core.session import ExperimentSession
from hopwins.storage.recorder import SessionRecorder


class SessionRecorderTests(unittest.TestCase):
    def test_session_preserves_raw_chunks_parameters_and_rows(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "experiment"
            session = ExperimentSession.create(
                path,
                task="session_capture",
                category="capture",
                mode="online",
                protocol="hcir_v2",
                device="follower",
                parameters={"duration_s": 10},
                effective_config={"app": {"task": "session_capture"}},
                label="test",
                notes=None,
                source="serial:COM5",
            )
            recorder = SessionRecorder(session)
            recorder.write_chunk(ByteChunk.now(b"abc", "serial:COM5"))
            recorder.write_row("values", ("x", "y"), {"x": 1, "y": 2})
            recorder.close()
            session.finish("complete", {"records": 1})

            manifest = json.loads((path / "manifest.json").read_text())
            raw = (path / "raw" / "serial.bin").read_bytes()
            rows = (path / "records" / "values.csv").read_text()

        self.assertEqual(manifest["status"], "complete")
        self.assertEqual(manifest["parameters"]["duration_s"], 10)
        self.assertIn("git_dirty", manifest)
        self.assertEqual(raw, b"abc")
        self.assertIn("x,y", rows)
        self.assertIn("1,2", rows)


if __name__ == "__main__":
    unittest.main()
