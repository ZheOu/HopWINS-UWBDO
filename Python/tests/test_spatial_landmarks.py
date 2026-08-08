from __future__ import annotations

import os

import numpy as np
import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6 import QtWidgets

from hopwins.analysis.spatial_landmarks import (
    SpatialLandmarkConfig,
    analyze_spatial_landmarks,
)
from hopwins.capture.assembler import CirCapture, CirCaptureAssembler
from hopwins.protocol.packets import CirSource, PacketType, parse_hcir_packet
from hopwins.tasks.spatial_timing_analysis import _write_csv
from hopwins.ui.spatial_timing_window import (
    SpatialTimingWindow,
    _dynamic_histogram_edges,
)
from tests.helpers import build_v3_packet


def test_spatial_landmarks_reject_fpi_jitter_with_paired_hcir_v3() -> None:
    captures = [
        _dual_capture(
            capture_id=index,
            sequence=1000 + index,
            delayed_time=0x10000000 + index * 24_960_000,
            fpi_error_samples=fpi_error,
        )
        for index, fpi_error in enumerate((0, 2, -3, 1, 0, -2))
    ]

    result = analyze_spatial_landmarks(
        _flatten(captures),
        config=_config(template_frames=3),
    )

    assert result.frame_count == 6
    assert len(result.landmarks) >= 2
    assert np.std(result.rx_error_dtu) > 50.0
    assert np.std(result.fpi_error_dtu) > 50.0
    assert np.std(result.cir_correlation_error_dtu) < 1e-6
    assert np.all(result.cir_correlation_score > 0.99)
    assert np.count_nonzero(np.isfinite(result.landmark_error_dtu)) == 6
    assert np.std(result.landmark_error_dtu) < 1e-6
    assert np.all(result.landmark_inlier_count >= 2)
    np.testing.assert_allclose(result.hardware_pdoa_radians, -0.5)
    assert np.all(result.landmark_track_reliability >= 0.99)
    assert result.landmark_shift_dtu.shape == (6, len(result.landmarks))
    assert result.landmark_inlier_mask.shape == (6, len(result.landmarks))


def test_spatial_landmarks_are_invariant_to_common_packet_phase_rotation() -> None:
    captures = [
        _dual_capture(
            capture_id=index,
            sequence=1500 + index,
            delayed_time=0x18000000 + index * 24_960_000,
            fpi_error_samples=0,
            common_phase_radians=phase,
        )
        for index, phase in enumerate((0.0, 0.6, -1.1, 2.3, -2.5, 0.9))
    ]

    result = analyze_spatial_landmarks(
        _flatten(captures),
        config=_config(template_frames=3),
    )

    assert np.count_nonzero(np.isfinite(result.landmark_error_dtu)) == 6
    assert np.std(result.landmark_error_dtu) < 1e-4


def test_spatial_landmarks_use_physical_rf_ports_not_sts_source_order() -> None:
    captures = [
        _dual_capture(
            capture_id=index,
            sequence=1750 + index,
            delayed_time=0x1C000000 + index * 24_960_000,
            fpi_error_samples=0,
            reverse_rf_ports=True,
        )
        for index in range(4)
    ]

    result = analyze_spatial_landmarks(
        _flatten(captures),
        config=_config(template_frames=2),
    )

    assert (result.rf1_port, result.rf2_port) == (1, 2)
    assert np.count_nonzero(np.isfinite(result.landmark_error_dtu)) == 4


def test_spatial_consensus_ignores_one_moving_reflection() -> None:
    captures = [
        _dual_capture(
            capture_id=index,
            sequence=2000 + index,
            delayed_time=0x20000000 + index * 24_960_000,
            fpi_error_samples=0,
            moved_reflection_samples=3 if index == 4 else 0,
        )
        for index in range(7)
    ]

    result = analyze_spatial_landmarks(
        _flatten(captures),
        config=_config(template_frames=3),
    )

    assert np.count_nonzero(np.isfinite(result.landmark_error_dtu)) == 7
    assert abs(result.landmark_error_dtu[4]) < 1.0
    assert result.landmark_inlier_count[4] >= 2


def test_spatial_analysis_requires_complete_dual_sts_pairs() -> None:
    captures = _dual_capture(
        capture_id=1,
        sequence=1,
        delayed_time=0x10000000,
        fpi_error_samples=0,
    )

    with pytest.raises(ValueError, match="at least two complete HCIR v3"):
        analyze_spatial_landmarks(captures[:1], config=_config(template_frames=2))


def test_dynamic_histogram_range_follows_the_result() -> None:
    edges = _dynamic_histogram_edges(np.asarray([-12.0, 3.0]), 2)

    assert edges[0] < -12.0
    assert edges[-1] > 12.0


def test_spatial_timing_window_builds_with_dynamic_ranges() -> None:
    captures = [
        _dual_capture(
            capture_id=index,
            sequence=3000 + index,
            delayed_time=0x30000000 + index * 24_960_000,
            fpi_error_samples=index % 2,
        )
        for index in range(4)
    ]
    result = analyze_spatial_landmarks(
        _flatten(captures),
        config=_config(template_frames=2),
    )
    application = QtWidgets.QApplication.instance()
    if application is None:
        application = QtWidgets.QApplication([])

    window = SpatialTimingWindow(result, "synthetic.hcir", histogram_bin_dtu=1)

    assert "Spatial CIR Timing" in window.windowTitle()
    window.close()


def test_spatial_analysis_exports_frame_and_landmark_diagnostics(tmp_path) -> None:
    captures = [
        _dual_capture(
            capture_id=index,
            sequence=4000 + index,
            delayed_time=0x40000000 + index * 24_960_000,
            fpi_error_samples=0,
        )
        for index in range(4)
    ]
    result = analyze_spatial_landmarks(
        _flatten(captures),
        config=_config(template_frames=2),
    )

    output_path = tmp_path / "run.spatial-timing.csv"
    landmark_path = _write_csv(output_path, result)

    assert output_path.exists()
    assert landmark_path == tmp_path / "run.spatial-timing.landmarks.csv"
    assert len(landmark_path.read_text(encoding="utf-8").splitlines()) == (
        1 + result.frame_count * len(result.landmarks)
    )


def _config(*, template_frames: int) -> SpatialLandmarkConfig:
    return SpatialLandmarkConfig(
        template_frames=template_frames,
        maximum_landmarks=4,
        landmark_window_samples=7,
        landmark_min_separation_samples=12,
        search_half_range_ns=6.0,
        interpolation_step_dtu=4,
        minimum_match_score=0.50,
        minimum_consensus_inliers=2,
        consensus_half_range_ns=0.6,
        minimum_rank_one_score=0.20,
    )


def _dual_capture(
    *,
    capture_id: int,
    sequence: int,
    delayed_time: int,
    fpi_error_samples: int,
    moved_reflection_samples: int = 0,
    common_phase_radians: float = 0.0,
    reverse_rf_ports: bool = False,
) -> list[CirCapture]:
    sample_count = 128
    sample_offset = 80
    antenna_delay = 16_416
    scheduled_timestamp = delayed_time << 8
    raw_timestamp = scheduled_timestamp + 250_000
    fpi_q10_6 = (128 + fpi_error_samples) * 64
    source_timestamp = raw_timestamp + fpi_q10_6 - antenna_delay
    frame = _tx_frame(sequence, delayed_time)
    source_payloads = (
        _cir_payload(
            sample_offset,
            sample_count,
            antenna=1,
            moved_reflection_samples=moved_reflection_samples,
            common_phase_radians=common_phase_radians,
        ),
        _cir_payload(
            sample_offset,
            sample_count,
            antenna=2,
            moved_reflection_samples=moved_reflection_samples,
            common_phase_radians=common_phase_radians,
        ),
    )
    captures: list[CirCapture] = []
    ports = (2, 1) if reverse_rf_ports else (1, 2)
    for source, port, payload in zip(
        (CirSource.STS0, CirSource.STS1),
        ports,
        source_payloads,
        strict=True,
    ):
        frame_packet = parse_hcir_packet(
            build_v3_packet(
                PacketType.RX_FRAME,
                capture_id=capture_id,
                cir_source=source,
                rf_port=port,
                payload=frame,
                capture_sample_offset=sample_offset,
                capture_sample_count=sample_count,
                raw_rx_timestamp=raw_timestamp,
                rx_timestamp=source_timestamp,
                first_path_index_q10_6=fpi_q10_6,
                rx_antenna_delay=antenna_delay,
                sts0_timestamp=source_timestamp,
                sts1_timestamp=source_timestamp,
            )
        )
        cir_packet = parse_hcir_packet(
            build_v3_packet(
                PacketType.CIR_DATA,
                capture_id=capture_id,
                cir_source=source,
                rf_port=port,
                payload=payload,
                capture_sample_offset=sample_offset,
                payload_sample_offset=sample_offset,
                payload_sample_count=sample_count,
                capture_sample_count=sample_count,
                raw_rx_timestamp=raw_timestamp,
                rx_timestamp=source_timestamp,
                first_path_index_q10_6=fpi_q10_6,
                rx_antenna_delay=antenna_delay,
                sts0_timestamp=source_timestamp,
                sts1_timestamp=source_timestamp,
            )
        )
        assembler = CirCaptureAssembler()
        assert assembler.add(frame_packet) is None
        capture = assembler.add(cir_packet)
        assert capture is not None
        captures.append(capture)
    return captures


def _flatten(groups: list[list[CirCapture]]) -> list[CirCapture]:
    return [capture for group in groups for capture in group]


def _tx_frame(sequence: int, delayed_time: int) -> bytes:
    frame = bytearray(23)
    frame[9:13] = b"HWDO"
    frame[13:17] = sequence.to_bytes(4, "little")
    frame[17:21] = delayed_time.to_bytes(4, "little")
    return bytes(frame)


def _cir_payload(
    sample_offset: int,
    sample_count: int,
    *,
    antenna: int,
    moved_reflection_samples: int,
    common_phase_radians: float,
) -> bytes:
    values = np.zeros(sample_count, dtype=np.complex128)
    structures = (
        (108, 26_000.0 + 8_000.0j),
        (135, -17_000.0 + 25_000.0j),
        (166 + moved_reflection_samples, 19_000.0 - 16_000.0j),
    )
    phase = np.exp(
        1j * (common_phase_radians + (0.55 if antenna == 2 else 0.0))
    )
    gain = 0.82 if antenna == 2 else 1.0
    for absolute_index, amplitude in structures:
        relative_index = absolute_index - sample_offset
        for offset, weight in ((-1, 0.25), (0, 1.0), (1, 0.40)):
            values[relative_index + offset] += amplitude * gain * phase * weight
    return b"".join(
        _pack_i24(int(np.rint(value.real))) + _pack_i24(int(np.rint(value.imag)))
        for value in values
    )


def _pack_i24(value: int) -> bytes:
    return (value & 0xFFFFFF).to_bytes(3, "little")
