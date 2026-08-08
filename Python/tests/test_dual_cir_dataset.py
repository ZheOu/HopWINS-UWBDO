from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

from hopwins.capture.assembler import CirCapture, CirCaptureAssembler
from hopwins.capture.pairing import DualCirCapture
from hopwins.core.session import ExperimentSession
from hopwins.protocol.packets import CirSource, PacketType, parse_hcir_packet
from hopwins.storage.dataset import DualCirDatasetReader, DualCirDatasetWriter
from hopwins.storage.recorder import SessionRecorder
from tests.helpers import build_v3_packet


class DualCirDatasetTests(unittest.TestCase):
    def test_empty_dataset_is_still_readable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dataset"
            session = _session(path)
            recorder = SessionRecorder(session)
            writer = DualCirDatasetWriter(recorder)
            writer.close()
            recorder.close()

            reader = DualCirDatasetReader(path)

        self.assertEqual(len(reader), 0)

    def test_writer_and_reader_preserve_paired_iq_samples(self) -> None:
        pair = DualCirCapture(
            capture_id=41,
            sts0=_capture(CirSource.STS0, 1, ((1, -2), (3, -4))),
            sts1=_capture(CirSource.STS1, 2, ((-5, 6), (-7, 8))),
        )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dataset"
            session = _session(path)
            recorder = SessionRecorder(session)
            writer = DualCirDatasetWriter(
                recorder,
                label="los-3m",
                flush_every_pairs=1,
            )
            writer.write_pair(
                pair,
                sts0_host_utc_ns=100,
                sts0_host_monotonic_ns=10,
                sts1_host_utc_ns=101,
                sts1_host_monotonic_ns=11,
            )
            writer.close()
            recorder.close()
            session.finish("complete", {"complete_pairs": 1})

            reader = DualCirDatasetReader(path)
            sample = reader.read_pair(0)
            metadata = json.loads(
                (path / "artifacts" / "dataset.json").read_text(encoding="utf-8")
            )

        self.assertEqual(len(reader), 1)
        self.assertEqual(sample.metadata["capture_id"], "41")
        self.assertEqual(sample.metadata["label"], "los-3m")
        np.testing.assert_array_equal(sample.sts0, ((1, -2), (3, -4)))
        np.testing.assert_array_equal(sample.sts1, ((-5, 6), (-7, 8)))
        self.assertEqual(metadata["iq_shape"], [4, 2])
        self.assertEqual(metadata["pair_count"], 1)
        self.assertTrue(metadata["closed"])


def _capture(
    source: CirSource,
    rf_port: int,
    samples: tuple[tuple[int, int], ...],
) -> CirCapture:
    assembler = CirCaptureAssembler()
    frame = parse_hcir_packet(
        build_v3_packet(
            PacketType.RX_FRAME,
            capture_id=41,
            cir_source=source,
            rf_port=rf_port,
            payload=b"frame",
            capture_sample_count=len(samples),
        )
    )
    payload = b"".join(_i24(i_value) + _i24(q_value) for i_value, q_value in samples)
    chunk = parse_hcir_packet(
        build_v3_packet(
            PacketType.CIR_DATA,
            capture_id=41,
            cir_source=source,
            rf_port=rf_port,
            payload=payload,
            payload_sample_count=len(samples),
            capture_sample_count=len(samples),
        )
    )
    if assembler.add(frame) is not None:
        raise AssertionError("frame unexpectedly completed before its CIR chunk")
    capture = assembler.add(chunk)
    if capture is None:
        raise AssertionError("test CIR capture did not assemble")
    return capture


def _session(path: Path) -> ExperimentSession:
    return ExperimentSession.create(
        path,
        task="dual_cir_dataset",
        category="diagnostic",
        mode="online",
        protocol="hcir_v3",
        device="sts_dual_rx",
        parameters={"duration_s": 10},
        effective_config={},
        label="los-3m",
        notes=None,
        source="serial:COM4",
    )


def _i24(value: int) -> bytes:
    return (value & 0xFFFFFF).to_bytes(3, "little")


if __name__ == "__main__":
    unittest.main()
