"""Compact, append-only storage for paired HCIR v3 STS CIR samples."""

from __future__ import annotations

import csv
import json
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import BinaryIO

import numpy as np
from numpy.typing import NDArray

from hopwins.analysis.cir import decode_i24_q24
from hopwins.capture.assembler import CirCapture
from hopwins.capture.pairing import DualCirCapture
from hopwins.protocol.packets import PacketFlags
from hopwins.storage.recorder import SessionRecorder

DATASET_SCHEMA = "hopwins.dual_cir.v1"
PAIR_TABLE = "records/dual_cir_pairs.csv"
IQ_FILE = "artifacts/cir_iq_i32le.bin"
METADATA_FILE = "artifacts/dataset.json"

PAIR_FIELDS = (
    "sample_index",
    "capture_id",
    "label",
    "protocol_version",
    "host_utc_ns",
    "host_monotonic_ns",
    "frame_match",
    "frame_hex",
    "quality_ok",
    "reference_time_ms",
    "ipatov_timestamp",
    "sts0_timestamp",
    "sts1_timestamp",
    "pdoa_radians",
    "pdoa_q1_11",
    "tdoa_dtu",
    "pdoa_diagnostic_status",
    "ipatov_status",
    "sts0_status",
    "sts1_status",
    "ipatov_phase",
    "sts0_phase",
    "sts1_phase",
)

CHANNEL_FIELDS = (
    "iq_offset_samples",
    "sample_count",
    "host_utc_ns",
    "host_monotonic_ns",
    "capture_sample_offset",
    "first_path_index",
    "first_path_relative",
    "peak_index",
    "rf_port",
    "rx_timestamp",
    "raw_rx_timestamp",
    "rssi_dbm",
    "first_path_power_dbm",
    "carrier_integrator",
    "clock_offset",
    "mcu_system_time_ms",
    "flags",
    "diagnostic_status",
    "cir_status",
    "register_status",
    "received_chunks",
    "expected_chunks",
)

DUAL_CIR_FIELDS = PAIR_FIELDS + tuple(
    f"{source}_{field}" for source in ("sts0", "sts1") for field in CHANNEL_FIELDS
)


@dataclass(frozen=True, slots=True)
class DatasetPair:
    """One indexed dataset row and its two ``[sample, I/Q]`` arrays."""

    metadata: dict[str, str]
    sts0: NDArray[np.int32]
    sts1: NDArray[np.int32]


class DualCirDatasetWriter:
    """Append paired CIR arrays while keeping offsets in a CSV table."""

    def __init__(
        self,
        recorder: SessionRecorder,
        *,
        label: str | None = None,
        flush_every_pairs: int = 10,
    ) -> None:
        if flush_every_pairs <= 0:
            raise ValueError("flush_every_pairs must be positive")
        self.recorder = recorder
        self.session_path = recorder.session.path
        self.label = label or ""
        self.flush_every_pairs = flush_every_pairs
        self._iq: BinaryIO = (self.session_path / IQ_FILE).open("wb")
        self.pair_count = 0
        self.total_iq_samples = 0
        self._closed = False
        self.recorder.ensure_table("dual_cir_pairs", DUAL_CIR_FIELDS)
        self._write_metadata(closed=False)

    def write_pair(
        self,
        pair: DualCirCapture,
        *,
        sts0_host_utc_ns: int,
        sts0_host_monotonic_ns: int,
        sts1_host_utc_ns: int,
        sts1_host_monotonic_ns: int,
    ) -> None:
        if self._closed:
            raise RuntimeError("dataset writer is closed")

        sts0_offset, sts0_count = self._write_channel(pair.sts0)
        sts1_offset, sts1_count = self._write_channel(pair.sts1)
        header = pair.sts0.header
        row: dict[str, object] = {
            "sample_index": self.pair_count,
            "capture_id": pair.capture_id,
            "label": self.label,
            "protocol_version": header.version,
            "host_utc_ns": max(sts0_host_utc_ns, sts1_host_utc_ns),
            "host_monotonic_ns": max(
                sts0_host_monotonic_ns,
                sts1_host_monotonic_ns,
            ),
            "frame_match": int(pair.sts0.frame == pair.sts1.frame),
            "frame_hex": pair.sts0.frame.hex(),
            "quality_ok": int(_quality_ok(pair)),
            "reference_time_ms": header.reference_time_ms,
            "ipatov_timestamp": header.ipatov_timestamp,
            "sts0_timestamp": header.sts0_timestamp,
            "sts1_timestamp": header.sts1_timestamp,
            "pdoa_radians": header.pdoa_radians,
            "pdoa_q1_11": header.pdoa_q1_11,
            "tdoa_dtu": header.tdoa_dtu,
            "pdoa_diagnostic_status": header.pdoa_diagnostic_status,
            "ipatov_status": header.ipatov_status,
            "sts0_status": header.sts0_status,
            "sts1_status": header.sts1_status,
            "ipatov_phase": header.ipatov_phase,
            "sts0_phase": header.sts0_phase,
            "sts1_phase": header.sts1_phase,
        }
        row.update(
            _channel_metadata(
                "sts0",
                pair.sts0,
                sts0_offset,
                sts0_count,
                sts0_host_utc_ns,
                sts0_host_monotonic_ns,
            )
        )
        row.update(
            _channel_metadata(
                "sts1",
                pair.sts1,
                sts1_offset,
                sts1_count,
                sts1_host_utc_ns,
                sts1_host_monotonic_ns,
            )
        )
        self.recorder.write_row("dual_cir_pairs", DUAL_CIR_FIELDS, row)
        self.pair_count += 1

        if self.pair_count % self.flush_every_pairs == 0:
            self.flush()

    def flush(self) -> None:
        if self._closed:
            return
        self._iq.flush()
        self.recorder.flush()
        self._write_metadata(closed=False)

    def close(self) -> None:
        if self._closed:
            return
        self.flush()
        self._iq.close()
        self._closed = True
        self._write_metadata(closed=True)

    def _write_channel(self, capture: CirCapture) -> tuple[int, int]:
        header = capture.header
        if not capture.has_cir:
            raise ValueError("dataset pair contains a channel without valid CIR")
        if header.bytes_per_sample != 6 or header.cir_format != 1:
            raise ValueError(
                "dataset requires signed little-endian I24/Q24 CIR samples"
            )

        i_values, q_values = decode_i24_q24(capture.cir_bytes)
        if len(i_values) != header.capture_sample_count:
            raise ValueError("assembled CIR sample count does not match its header")
        samples = np.empty((len(i_values), 2), dtype=np.dtype("<i4"))
        samples[:, 0] = i_values
        samples[:, 1] = q_values

        offset = self.total_iq_samples
        self._iq.write(samples.tobytes(order="C"))
        self.total_iq_samples += len(samples)
        return offset, len(samples)

    def _write_metadata(self, *, closed: bool) -> None:
        metadata = {
            "schema": DATASET_SCHEMA,
            "closed": closed,
            "updated_utc": datetime.now(UTC).isoformat(),
            "pair_table": PAIR_TABLE,
            "iq_file": IQ_FILE,
            "iq_dtype": "<i4",
            "iq_components": ["i", "q"],
            "iq_shape": [self.total_iq_samples, 2],
            "offset_unit": "complex_samples",
            "pair_count": self.pair_count,
        }
        destination = self.session_path / METADATA_FILE
        temporary = destination.with_suffix(".json.tmp")
        temporary.write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary.replace(destination)


class DualCirDatasetReader:
    """Read indexed STS0/STS1 arrays without decoding the raw UART stream."""

    def __init__(self, path: str | Path) -> None:
        self.path = Path(path).expanduser().resolve()
        metadata_path = self.path / METADATA_FILE
        if not metadata_path.is_file():
            raise ValueError(f"dual CIR dataset metadata not found: {metadata_path}")
        self.metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if self.metadata.get("schema") != DATASET_SCHEMA:
            schema = self.metadata.get("schema")
            raise ValueError(f"unsupported dataset schema: {schema}")

        pair_table = self.path / str(self.metadata["pair_table"])
        with pair_table.open("r", encoding="utf-8", newline="") as stream:
            self.rows = tuple(dict(row) for row in csv.DictReader(stream))
        self.iq_path = self.path / str(self.metadata["iq_file"])

    def __len__(self) -> int:
        return len(self.rows)

    def read_pair(self, index: int) -> DatasetPair:
        row = self.rows[index]
        return DatasetPair(
            metadata=dict(row),
            sts0=self._read_channel(row, "sts0"),
            sts1=self._read_channel(row, "sts1"),
        )

    def _read_channel(
        self,
        row: dict[str, str],
        prefix: str,
    ) -> NDArray[np.int32]:
        offset = int(row[f"{prefix}_iq_offset_samples"])
        count = int(row[f"{prefix}_sample_count"])
        with self.iq_path.open("rb") as stream:
            stream.seek(offset * 2 * np.dtype("<i4").itemsize)
            values = np.fromfile(stream, dtype="<i4", count=count * 2)
        if values.size != count * 2:
            sample_index = row["sample_index"]
            raise ValueError(f"truncated CIR array for dataset row {sample_index}")
        return values.reshape((count, 2)).astype(np.int32, copy=False)


def _channel_metadata(
    prefix: str,
    capture: CirCapture,
    iq_offset: int,
    sample_count: int,
    host_utc_ns: int,
    host_monotonic_ns: int,
) -> dict[str, object]:
    header = capture.header
    values = {
        "iq_offset_samples": iq_offset,
        "sample_count": sample_count,
        "host_utc_ns": host_utc_ns,
        "host_monotonic_ns": host_monotonic_ns,
        "capture_sample_offset": header.capture_sample_offset,
        "first_path_index": header.first_path_index,
        "first_path_relative": (header.first_path_index - header.capture_sample_offset),
        "peak_index": header.peak_index,
        "rf_port": header.cir_source_rf_port,
        "rx_timestamp": header.rx_timestamp,
        "raw_rx_timestamp": (
            header.raw_rx_timestamp if header.raw_rx_timestamp_valid else None
        ),
        "rssi_dbm": header.rssi_dbm,
        "first_path_power_dbm": header.first_path_power_dbm,
        "carrier_integrator": header.carrier_integrator,
        "clock_offset": header.clock_offset,
        "mcu_system_time_ms": header.mcu_system_time_ms,
        "flags": int(header.flags),
        "diagnostic_status": header.diagnostic_status,
        "cir_status": header.cir_status,
        "register_status": header.register_status,
        "received_chunks": capture.received_chunks,
        "expected_chunks": capture.expected_chunks,
    }
    return {f"{prefix}_{key}": value for key, value in values.items()}


def _quality_ok(pair: DualCirCapture) -> bool:
    headers = (pair.sts0.header, pair.sts1.header)
    return (
        pair.sts0.has_cir
        and pair.sts1.has_cir
        and pair.sts0.frame == pair.sts1.frame
        and all(
            header.flags & PacketFlags.PDOA_DIAGNOSTIC_VALID
            and header.diagnostic_status == 0
            and header.cir_status == 0
            and header.register_status == 0
            and header.pdoa_diagnostic_status == 0
            for header in headers
        )
    )
