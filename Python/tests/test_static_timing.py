from __future__ import annotations

import numpy as np
import pytest

from hopwins.analysis.static_timing import (
    CIR_SAMPLE_DTU,
    CirCorrelationConfig,
    analyze_static_timing,
    parse_transmit_schedule,
)
from hopwins.capture.assembler import CirCapture, CirCaptureAssembler
from hopwins.protocol.packets import PacketType, parse_hcir_packet
from hopwins.ui.static_timing_window import _histogram_half_range_ns
from tests.helpers import build_v2_packet


def test_parse_transmit_schedule_from_hwdo_frame() -> None:
    frame = _tx_frame(sequence=0x12345678, delayed_time=0x90ABCDEF)

    schedule = parse_transmit_schedule(frame)

    assert schedule is not None
    assert schedule.sequence == 0x12345678
    assert schedule.delayed_time == 0x90ABCDEF
    assert schedule.timestamp_dtu == 0x90ABCDEF00
    assert parse_transmit_schedule(b"not a HopWINS frame") is None


def test_static_analysis_preserves_timing_jitter_without_smearing_cir() -> None:
    jitters = (0, 0, 192, 192)
    captures = [
        _capture(
            capture_id=index,
            sequence=100 + index,
            delayed_time=0x10000000 + index * 24_960_000,
            timing_jitter_dtu=jitter,
        )
        for index, jitter in enumerate(jitters)
    ]

    result = analyze_static_timing(captures)

    np.testing.assert_array_equal(result.rx_error_dtu, [-96, -96, 96, 96])
    np.testing.assert_allclose(
        result.fpi_equivalent_error_dtu,
        [-96, -96, 96, 96],
    )
    assert result.frame_count == 4
    assert result.missing_sequence_count == 0
    assert result.transmit_interval_us == pytest.approx(100_000.0)
    assert result.common_time_dtu[np.argmax(result.mean_power_db)] in (128, 192)
    np.testing.assert_allclose(result.mean_power_db, result.first_frame_power_db)
    np.testing.assert_allclose(result.cir_similarity, 1.0)
    assert np.std(result.cir_match_error_dtu) < 1e-6
    assert np.min(result.cir_match_score) > 0.99


def test_cir_match_tracks_a_physical_profile_shift_with_the_right_sign() -> None:
    captures = [
        _capture(
            capture_id=index,
            sequence=200 + index,
            delayed_time=0x20000000 + index * 24_960_000,
            timing_jitter_dtu=0,
            channel_shift_samples=shift,
        )
        for index, shift in enumerate((0, 0, 1, 1))
    ]

    result = analyze_static_timing(
        captures,
        correlation=CirCorrelationConfig(template_frames=2),
    )

    assert np.mean(result.cir_match_error_dtu[:2]) < 0.0
    assert np.mean(result.cir_match_error_dtu[2:]) > 0.0
    assert np.ptp(result.cir_match_error_dtu) == pytest.approx(
        CIR_SAMPLE_DTU,
        abs=2.0,
    )


def test_static_analysis_rejects_mixed_rf_ports() -> None:
    captures = [
        _capture(
            capture_id=port,
            sequence=port,
            delayed_time=0x10000000 + port * 24_960_000,
            timing_jitter_dtu=0,
            rf_port=port,
        )
        for port in (1, 2)
    ]

    with pytest.raises(ValueError, match="multiple RF ports"):
        analyze_static_timing(captures)

    selected = analyze_static_timing(captures * 2, rf_port=2)
    assert selected.rf_port == 2


def test_histogram_range_stays_compact_and_expands_for_outliers() -> None:
    assert _histogram_half_range_ns(np.asarray([-0.1, 0.2]), 10.0) == 10.0
    assert _histogram_half_range_ns(np.asarray([-12.0, 3.0]), 10.0) == 14.0


def _capture(
    *,
    capture_id: int,
    sequence: int,
    delayed_time: int,
    timing_jitter_dtu: int,
    rf_port: int = 1,
    channel_shift_samples: int = 0,
) -> CirCapture:
    rx_antenna_delay = 16_416
    cia_fpi_constant = -41_408
    first_path_q10_6 = 128 * CIR_SAMPLE_DTU + timing_jitter_dtu
    scheduled_timestamp = delayed_time << 8
    rx_timestamp = scheduled_timestamp + 250_000 + timing_jitter_dtu
    raw_timestamp = (
        rx_timestamp
        + rx_antenna_delay
        - first_path_q10_6
        - cia_fpi_constant
    )
    samples = [(0, 0)] * 96
    samples[48 + channel_shift_samples] = (20_000, -10_000)
    samples[52 + channel_shift_samples] = (30_000, 5_000)
    samples[65 + channel_shift_samples] = (-12_000, 8_000)
    cir_payload = b"".join(
        _pack_i24(i_value) + _pack_i24(q_value)
        for i_value, q_value in samples
    )

    frame_packet = parse_hcir_packet(
        build_v2_packet(
            PacketType.RX_FRAME,
            capture_id=capture_id,
            payload=_tx_frame(sequence, delayed_time),
            capture_sample_offset=80,
            capture_sample_count=len(samples),
            rx_timestamp=rx_timestamp,
            raw_rx_timestamp=raw_timestamp,
            first_path_index_q10_6=first_path_q10_6,
            rx_antenna_delay=rx_antenna_delay,
            rf_port=rf_port,
        )
    )
    cir_packet = parse_hcir_packet(
        build_v2_packet(
            PacketType.CIR_DATA,
            capture_id=capture_id,
            payload=cir_payload,
            capture_sample_offset=80,
            payload_sample_offset=80,
            payload_sample_count=len(samples),
            capture_sample_count=len(samples),
            rx_timestamp=rx_timestamp,
            raw_rx_timestamp=raw_timestamp,
            first_path_index_q10_6=first_path_q10_6,
            rx_antenna_delay=rx_antenna_delay,
            rf_port=rf_port,
        )
    )
    assembler = CirCaptureAssembler()
    assert assembler.add(frame_packet) is None
    capture = assembler.add(cir_packet)
    assert capture is not None
    return capture


def _tx_frame(sequence: int, delayed_time: int) -> bytes:
    frame = bytearray(23)
    frame[9:13] = b"HWDO"
    frame[13:17] = sequence.to_bytes(4, "little")
    frame[17:21] = delayed_time.to_bytes(4, "little")
    return bytes(frame)


def _pack_i24(value: int) -> bytes:
    return (value & 0xFFFFFF).to_bytes(3, "little")
