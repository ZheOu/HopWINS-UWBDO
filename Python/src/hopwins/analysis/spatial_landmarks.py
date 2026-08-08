"""Dual-antenna CIR landmark timing for HCIR v3 STS captures.

The estimator treats each persistent local CIR structure as a landmark.  A
landmark is matched independently on RF1 and RF2, then the per-landmark time
shifts are combined only when they agree.  This keeps a changing reflection
from moving the resulting timestamp by itself.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass

import numpy as np
from numpy.typing import NDArray

from hopwins.analysis.cir import decode_i24_q24
from hopwins.analysis.static_timing import (
    CIR_SAMPLE_DTU,
    DW_TIMESTAMP_MODULUS,
    TransmitSchedule,
    parse_transmit_schedule,
    signed_timestamp_delta,
)
from hopwins.analysis.statistics import DW_TIME_UNIT_SECONDS
from hopwins.capture.assembler import CirCapture
from hopwins.protocol.packets import CirSource


@dataclass(frozen=True, slots=True)
class SpatialLandmarkConfig:
    """Controls for dual-CIR landmark detection and consensus."""

    template_frames: int = 50
    maximum_landmarks: int = 8
    landmark_window_samples: int = 13
    landmark_min_separation_samples: int = 10
    search_half_range_ns: float = 10.0
    interpolation_step_dtu: int = 4
    minimum_match_score: float = 0.60
    minimum_consensus_inliers: int = 2
    consensus_half_range_ns: float = 0.75
    minimum_rank_one_score: float = 0.35
    minimum_track_reliability: float = 0.55
    correlation_mode: str = "complex"
    correlation_noise_percentile: float = 20.0
    correlation_dynamic_range_db: float = 30.0
    clock_drift_mode: str = "linear"


@dataclass(frozen=True, slots=True)
class SpatialLandmark:
    """A template-local, spatially coherent CIR patch."""

    center_dtu: float
    quality: float
    coherence: float
    rank_one_score: float


@dataclass(frozen=True, slots=True)
class SpatialLandmarkResult:
    """Dual-CIR profile and the three comparable timestamp estimates."""

    source_capture_count: int
    paired_capture_count: int
    skipped_capture_count: int
    sequence: NDArray[np.int64]
    capture_id: NDArray[np.int64]
    scheduled_timestamp_dtu: NDArray[np.int64]
    rx_timestamp_dtu: NDArray[np.int64]
    fpi_timestamp_dtu: NDArray[np.int64]
    cir_correlation_timestamp_dtu: NDArray[np.float64]
    landmark_timestamp_dtu: NDArray[np.float64]
    rx_error_dtu: NDArray[np.float64]
    fpi_error_dtu: NDArray[np.float64]
    cir_correlation_error_dtu: NDArray[np.float64]
    landmark_error_dtu: NDArray[np.float64]
    rx_error_ns: NDArray[np.float64]
    fpi_error_ns: NDArray[np.float64]
    cir_correlation_error_ns: NDArray[np.float64]
    landmark_error_ns: NDArray[np.float64]
    cir_correlation_score: NDArray[np.float64]
    cir_correlation_peak_margin: NDArray[np.float64]
    landmark_quality: NDArray[np.float64]
    landmark_inlier_count: NDArray[np.int64]
    landmark_candidate_count: NDArray[np.int64]
    landmark_vote_dtu: NDArray[np.float64]
    landmark_track_reliability: NDArray[np.float64]
    landmark_shift_dtu: NDArray[np.float64]
    landmark_match_score: NDArray[np.float64]
    landmark_match_quality: NDArray[np.float64]
    landmark_match_margin: NDArray[np.float64]
    landmark_spatial_quality: NDArray[np.float64]
    landmark_coherence: NDArray[np.float64]
    landmark_rank_one_score: NDArray[np.float64]
    landmark_inlier_mask: NDArray[np.bool_]
    hardware_pdoa_radians: NDArray[np.float64]
    hardware_tdoa_dtu: NDArray[np.float64]
    cir_time_ns: NDArray[np.float64]
    rf1_mean_power_db: NDArray[np.float64]
    rf1_p10_power_db: NDArray[np.float64]
    rf1_p90_power_db: NDArray[np.float64]
    rf2_mean_power_db: NDArray[np.float64]
    rf2_p10_power_db: NDArray[np.float64]
    rf2_p90_power_db: NDArray[np.float64]
    landmarks: tuple[SpatialLandmark, ...]
    rf1_port: int
    rf2_port: int
    reference_landmark_dtu: float
    clock_frequency_error_ppb: float
    clock_drift_mode: str
    missing_sequence_count: int
    transmit_interval_us: float | None

    @property
    def frame_count(self) -> int:
        return int(self.sequence.size)


@dataclass(frozen=True, slots=True)
class _Pair:
    capture_id: int
    schedule: TransmitSchedule
    rf1: CirCapture
    rf2: CirCapture
    raw_timestamp: int
    axis1_dtu: NDArray[np.float64]
    axis2_dtu: NDArray[np.float64]
    cir1: NDArray[np.complex128]
    cir2: NDArray[np.complex128]


@dataclass(frozen=True, slots=True)
class _LandmarkMatch:
    shift_dtu: float
    score: float
    quality: float
    margin: float
    spatial_quality: float
    coherence: float
    rank_one_score: float


@dataclass(frozen=True, slots=True)
class _CirCorrelation:
    shift_dtu: NDArray[np.float64]
    score: NDArray[np.float64]
    peak_margin: NDArray[np.float64]


@dataclass(frozen=True, slots=True)
class _ClockModel:
    schedule_origin_dtu: float
    receiver_origin_dtu: float
    receiver_per_transmitter_dtu: float

    @property
    def frequency_error_ppb(self) -> float:
        return (self.receiver_per_transmitter_dtu - 1.0) * 1e9


def analyze_spatial_landmarks(
    captures: list[CirCapture],
    *,
    config: SpatialLandmarkConfig | None = None,
) -> SpatialLandmarkResult:
    """Analyze HCIR v3 STS0/STS1 capture pairs on physical RF1 and RF2.

    ``RAWST`` is the timestamp reference for the new estimator.  The STS ToA
    and source FPI only map each accumulator window onto that common reference;
    they do not select or align the landmark being measured.
    """
    analysis_config = config or SpatialLandmarkConfig()
    _validate_config(analysis_config)
    pairs, skipped = _build_pairs(captures)
    if len(pairs) < 2:
        raise ValueError(
            "spatial landmark analysis requires at least two complete HCIR v3 "
            "STS0 + STS1 dual-port pairs with RAW_TIMESTAMP_VALID HWDO frames"
        )

    common_grid = _common_grid(pairs)
    rf1_matrix = np.asarray(
        [_interpolate_complex(pair.axis1_dtu, pair.cir1, common_grid) for pair in pairs]
    )
    rf2_matrix = np.asarray(
        [_interpolate_complex(pair.axis2_dtu, pair.cir2, common_grid) for pair in pairs]
    )
    template_count = min(analysis_config.template_frames, len(pairs))
    template1, template2 = _joint_phase_aligned_template(
        rf1_matrix[:template_count],
        rf2_matrix[:template_count],
    )
    correlation_template = _phase_aligned_template(rf1_matrix[:template_count])
    landmarks = _detect_landmarks(
        template1,
        template2,
        common_grid,
        rf1_matrix[:template_count],
        rf2_matrix[:template_count],
        analysis_config,
    )
    if len(landmarks) < analysis_config.minimum_consensus_inliers:
        raise ValueError(
            "the training CIR does not contain enough spatially coherent "
            "landmarks; increase template_frames or relax the landmark settings"
        )

    reference_landmark_dtu = _weighted_median(
        np.asarray([landmark.center_dtu for landmark in landmarks]),
        np.asarray([landmark.quality for landmark in landmarks]),
    )
    cir_correlation = _match_full_cir(
        correlation_template,
        common_grid,
        rf1_matrix,
        analysis_config,
    )
    matches = _match_landmarks(
        template1,
        template2,
        common_grid,
        rf1_matrix,
        rf2_matrix,
        landmarks,
        analysis_config,
    )
    initial_consensus = _consensus_timing(
        matches,
        landmarks,
        analysis_config,
    )
    track_reliability = _track_reliability(
        matches,
        initial_consensus,
        analysis_config,
    )
    consensus = _consensus_timing(
        matches,
        landmarks,
        analysis_config,
        track_reliability=track_reliability,
    )

    scheduled = np.asarray(
        [pair.schedule.timestamp_dtu for pair in pairs],
        dtype=np.int64,
    )
    rx_timestamps = np.asarray(
        [pair.rf1.header.rx_timestamp for pair in pairs],
        dtype=np.int64,
    )
    fpi_timestamps = np.asarray(
        [
            (
                pair.raw_timestamp
                + pair.rf1.header.first_path_index_q10_6
                - pair.rf1.header.rx_antenna_delay
            )
            & (DW_TIMESTAMP_MODULUS - 1)
            for pair in pairs
        ],
        dtype=np.int64,
    )
    landmark_timestamps = np.full(len(pairs), np.nan, dtype=np.float64)
    valid_consensus = np.isfinite(consensus.shift_dtu)
    for index in np.flatnonzero(valid_consensus):
        landmark_timestamps[index] = float(
            (
                pairs[index].raw_timestamp
                + int(np.rint(reference_landmark_dtu + consensus.shift_dtu[index]))
            )
            & (DW_TIMESTAMP_MODULUS - 1)
        )

    cir_correlation_timestamps = np.full(len(pairs), np.nan, dtype=np.float64)
    valid_correlation = np.isfinite(cir_correlation.shift_dtu)
    for index in np.flatnonzero(valid_correlation):
        cir_correlation_timestamps[index] = float(
            (
                pairs[index].raw_timestamp
                + int(
                    np.rint(
                        reference_landmark_dtu
                        + cir_correlation.shift_dtu[index]
                    )
                )
            )
            & (DW_TIMESTAMP_MODULUS - 1)
        )

    clock_model = _fit_clock_model(
        np.asarray([pair.raw_timestamp for pair in pairs], dtype=np.int64),
        scheduled,
        analysis_config.clock_drift_mode,
    )
    rx_error = _clock_compensated_errors(
        rx_timestamps,
        scheduled,
        clock_model,
    )
    fpi_error = _clock_compensated_errors(
        fpi_timestamps,
        scheduled,
        clock_model,
    )
    cir_correlation_error = _clock_compensated_errors(
        cir_correlation_timestamps,
        scheduled,
        clock_model,
    )
    landmark_error = _clock_compensated_errors(
        landmark_timestamps,
        scheduled,
        clock_model,
    )

    combined_reference = max(
        float(np.max(np.abs(rf1_matrix) ** 2, initial=0.0)),
        float(np.max(np.abs(rf2_matrix) ** 2, initial=0.0)),
        1.0,
    )
    rf1_power = np.abs(rf1_matrix) ** 2
    rf2_power = np.abs(rf2_matrix) ** 2
    return SpatialLandmarkResult(
        source_capture_count=len(captures),
        paired_capture_count=len(pairs),
        skipped_capture_count=skipped,
        sequence=np.asarray([pair.schedule.sequence for pair in pairs], dtype=np.int64),
        capture_id=np.asarray([pair.capture_id for pair in pairs], dtype=np.int64),
        scheduled_timestamp_dtu=scheduled,
        rx_timestamp_dtu=rx_timestamps,
        fpi_timestamp_dtu=fpi_timestamps,
        cir_correlation_timestamp_dtu=cir_correlation_timestamps,
        landmark_timestamp_dtu=landmark_timestamps,
        rx_error_dtu=rx_error,
        fpi_error_dtu=fpi_error,
        cir_correlation_error_dtu=cir_correlation_error,
        landmark_error_dtu=landmark_error,
        rx_error_ns=_dtu_to_ns(rx_error),
        fpi_error_ns=_dtu_to_ns(fpi_error),
        cir_correlation_error_ns=_dtu_to_ns(cir_correlation_error),
        landmark_error_ns=_dtu_to_ns(landmark_error),
        cir_correlation_score=cir_correlation.score,
        cir_correlation_peak_margin=cir_correlation.peak_margin,
        landmark_quality=consensus.quality,
        landmark_inlier_count=consensus.inlier_count,
        landmark_candidate_count=consensus.candidate_count,
        landmark_vote_dtu=consensus.shift_dtu,
        landmark_track_reliability=track_reliability,
        landmark_shift_dtu=_match_matrix(matches, "shift_dtu"),
        landmark_match_score=_match_matrix(matches, "score"),
        landmark_match_quality=_match_matrix(matches, "quality"),
        landmark_match_margin=_match_matrix(matches, "margin"),
        landmark_spatial_quality=_match_matrix(matches, "spatial_quality"),
        landmark_coherence=_match_matrix(matches, "coherence"),
        landmark_rank_one_score=_match_matrix(matches, "rank_one_score"),
        landmark_inlier_mask=consensus.inlier_mask,
        hardware_pdoa_radians=np.asarray(
            [
                pair.rf1.header.pdoa_radians
                if pair.rf1.header.pdoa_diagnostic_valid
                and pair.rf1.header.pdoa_diagnostic_status == 0
                else np.nan
                for pair in pairs
            ],
            dtype=np.float64,
        ),
        hardware_tdoa_dtu=np.asarray(
            [
                float(pair.rf1.header.tdoa_dtu)
                if pair.rf1.header.pdoa_diagnostic_valid
                and pair.rf1.header.pdoa_diagnostic_status == 0
                else np.nan
                for pair in pairs
            ],
            dtype=np.float64,
        ),
        cir_time_ns=_dtu_to_ns(common_grid - reference_landmark_dtu),
        rf1_mean_power_db=_power_db(np.mean(rf1_power, axis=0), combined_reference),
        rf1_p10_power_db=_power_db(
            np.percentile(rf1_power, 10.0, axis=0),
            combined_reference,
        ),
        rf1_p90_power_db=_power_db(
            np.percentile(rf1_power, 90.0, axis=0),
            combined_reference,
        ),
        rf2_mean_power_db=_power_db(np.mean(rf2_power, axis=0), combined_reference),
        rf2_p10_power_db=_power_db(
            np.percentile(rf2_power, 10.0, axis=0),
            combined_reference,
        ),
        rf2_p90_power_db=_power_db(
            np.percentile(rf2_power, 90.0, axis=0),
            combined_reference,
        ),
        landmarks=tuple(landmarks),
        rf1_port=pairs[0].rf1.header.rf_port,
        rf2_port=pairs[0].rf2.header.rf_port,
        reference_landmark_dtu=reference_landmark_dtu,
        clock_frequency_error_ppb=clock_model.frequency_error_ppb,
        clock_drift_mode=analysis_config.clock_drift_mode,
        missing_sequence_count=_missing_sequence_count(
            np.asarray([pair.schedule.sequence for pair in pairs], dtype=np.int64)
        ),
        transmit_interval_us=_transmit_interval_us(pairs),
    )


def _build_pairs(captures: list[CirCapture]) -> tuple[list[_Pair], int]:
    grouped: dict[int, dict[CirSource, CirCapture]] = defaultdict(dict)
    invalid_ids: set[int] = set()
    for capture in captures:
        header = capture.header
        if (
            header.version < 3
            or header.cir_source not in (CirSource.STS0, CirSource.STS1)
            or not capture.has_cir
            or header.bytes_per_sample != 6
            or not header.raw_rx_timestamp_valid
            or header.first_path_index_q10_6 == 0
            or header.cir_timestamp == 0
        ):
            continue
        if header.cir_source in grouped[header.capture_id]:
            invalid_ids.add(header.capture_id)
            continue
        grouped[header.capture_id][header.cir_source] = capture

    pairs: list[_Pair] = []
    for capture_id, sources in grouped.items():
        if capture_id in invalid_ids:
            continue
        sts0 = sources.get(CirSource.STS0)
        sts1 = sources.get(CirSource.STS1)
        if sts0 is None or sts1 is None:
            continue
        if {sts0.header.rf_port, sts1.header.rf_port} != {1, 2}:
            continue
        paths = {
            sts0.header.rf_port: sts0,
            sts1.header.rf_port: sts1,
        }
        rf1 = paths[1]
        rf2 = paths[2]
        schedule = parse_transmit_schedule(rf1.frame)
        if schedule is None or parse_transmit_schedule(rf2.frame) != schedule:
            continue
        if rf1.header.raw_rx_timestamp != rf2.header.raw_rx_timestamp:
            continue
        cir1 = _decode_complex(rf1)
        cir2 = _decode_complex(rf2)
        if cir1.size == 0 or cir2.size == 0:
            continue
        pairs.append(
            _Pair(
                capture_id=capture_id,
                schedule=schedule,
                rf1=rf1,
                rf2=rf2,
                raw_timestamp=rf1.header.raw_rx_timestamp,
                axis1_dtu=_source_axis_dtu(rf1),
                axis2_dtu=_source_axis_dtu(rf2),
                cir1=cir1,
                cir2=cir2,
            )
        )
    pairs.sort(key=lambda pair: pair.schedule.sequence)
    return pairs, len(captures) - 2 * len(pairs)


def _decode_complex(capture: CirCapture) -> NDArray[np.complex128]:
    i_values, q_values = decode_i24_q24(capture.cir_bytes)
    return i_values.astype(np.float64) + 1j * q_values.astype(np.float64)


def _source_axis_dtu(capture: CirCapture) -> NDArray[np.float64]:
    header = capture.header
    accumulator_index = np.arange(
        header.capture_sample_count,
        dtype=np.float64,
    ) + float(header.capture_sample_offset)
    source_to_raw = signed_timestamp_delta(
        header.cir_timestamp,
        header.raw_rx_timestamp,
    )
    return (
        accumulator_index * CIR_SAMPLE_DTU
        + source_to_raw
        - header.first_path_index_q10_6
    )


def _common_grid(pairs: list[_Pair]) -> NDArray[np.float64]:
    lower = max(
        float(max(pair.axis1_dtu[0], pair.axis2_dtu[0])) for pair in pairs
    )
    upper = min(
        float(min(pair.axis1_dtu[-1], pair.axis2_dtu[-1])) for pair in pairs
    )
    start = np.ceil(lower / CIR_SAMPLE_DTU) * CIR_SAMPLE_DTU
    stop = np.floor(upper / CIR_SAMPLE_DTU) * CIR_SAMPLE_DTU
    if stop - start < 8 * CIR_SAMPLE_DTU:
        raise ValueError(
            "paired STS CIR windows have too little common physical-time coverage"
        )
    return np.arange(start, stop + CIR_SAMPLE_DTU, CIR_SAMPLE_DTU)


def _interpolate_complex(
    source_axis: NDArray[np.float64],
    source_values: NDArray[np.complex128],
    target_axis: NDArray[np.float64],
) -> NDArray[np.complex128]:
    return np.interp(target_axis, source_axis, source_values.real) + 1j * np.interp(
        target_axis,
        source_axis,
        source_values.imag,
    )


def _interpolate_uniform_shift_stack(
    values: NDArray[np.complex128],
    sample_indices: NDArray[np.int64],
    shifts_dtu: NDArray[np.float64],
) -> NDArray[np.complex128]:
    """Interpolate every candidate shift of a uniform 64-DTU CIR grid."""
    positions = sample_indices[None, :].astype(np.float64) + (
        shifts_dtu[:, None] / CIR_SAMPLE_DTU
    )
    positions = np.clip(positions, 0.0, float(values.size - 1))
    lower = np.floor(positions).astype(np.int64)
    fraction = positions - lower
    upper = np.minimum(lower + 1, values.size - 1)
    return values[lower] * (1.0 - fraction) + values[upper] * fraction


def _joint_phase_aligned_template(
    rf1: NDArray[np.complex128],
    rf2: NDArray[np.complex128],
) -> tuple[NDArray[np.complex128], NDArray[np.complex128]]:
    energy = np.sum(np.abs(rf1) ** 2 + np.abs(rf2) ** 2, axis=1)
    reference_index = int(np.argsort(energy)[len(energy) // 2])
    template1 = rf1[reference_index].copy()
    template2 = rf2[reference_index].copy()
    for _ in range(3):
        aligned1 = np.empty_like(rf1)
        aligned2 = np.empty_like(rf2)
        for index, (row1, row2) in enumerate(zip(rf1, rf2, strict=True)):
            correlation = np.vdot(template1, row1) + np.vdot(template2, row2)
            phase = np.angle(correlation) if correlation != 0.0 else 0.0
            rotation = np.exp(-1j * phase)
            aligned1[index] = row1 * rotation
            aligned2[index] = row2 * rotation
        template1 = np.mean(aligned1, axis=0)
        template2 = np.mean(aligned2, axis=0)
    return template1, template2


def _phase_aligned_template(
    rows: NDArray[np.complex128],
) -> NDArray[np.complex128]:
    energy = np.sum(np.abs(rows) ** 2, axis=1)
    template = rows[int(np.argsort(energy)[len(energy) // 2])].copy()
    for _ in range(3):
        aligned = np.empty_like(rows)
        for index, row in enumerate(rows):
            correlation = np.vdot(template, row)
            phase = np.angle(correlation) if correlation != 0.0 else 0.0
            aligned[index] = row * np.exp(-1j * phase)
        template = np.mean(aligned, axis=0)
    return template


def _detect_landmarks(
    template1: NDArray[np.complex128],
    template2: NDArray[np.complex128],
    grid: NDArray[np.float64],
    training1: NDArray[np.complex128],
    training2: NDArray[np.complex128],
    config: SpatialLandmarkConfig,
) -> list[SpatialLandmark]:
    half_window = config.landmark_window_samples // 2
    power = np.abs(template1) ** 2 + np.abs(template2) ** 2
    smoothed_power = np.convolve(power, np.ones(3) / 3.0, mode="same")
    candidates = [
        index
        for index in range(half_window, power.size - half_window)
        if smoothed_power[index] >= smoothed_power[index - 1]
        and smoothed_power[index] > smoothed_power[index + 1]
    ]
    if not candidates:
        candidates = [int(np.argmax(smoothed_power))]

    evaluated: list[tuple[float, int, SpatialLandmark]] = []
    noise_power = max(float(np.median(power)), 1.0)
    for index in candidates:
        start = index - half_window
        stop = index + half_window + 1
        quality, coherence, rank_one = _spatial_quality(
            template1[start:stop],
            template2[start:stop],
        )
        persistence = _training_persistence(
            template1[start:stop],
            template2[start:stop],
            training1[:, start:stop],
            training2[:, start:stop],
        )
        energy_score = np.log1p(smoothed_power[index] / noise_power)
        landmark = SpatialLandmark(
            center_dtu=float(grid[index]),
            quality=float(quality * persistence),
            coherence=float(coherence),
            rank_one_score=float(rank_one),
        )
        evaluated.append((energy_score * landmark.quality, index, landmark))

    selected: list[SpatialLandmark] = []
    selected_indices: list[int] = []
    for _, index, landmark in sorted(evaluated, reverse=True):
        if landmark.rank_one_score < config.minimum_rank_one_score:
            continue
        if any(
            abs(index - previous) < config.landmark_min_separation_samples
            for previous in selected_indices
        ):
            continue
        selected.append(landmark)
        selected_indices.append(index)
        if len(selected) == config.maximum_landmarks:
            break
    return sorted(selected, key=lambda landmark: landmark.center_dtu)


def _training_persistence(
    reference1: NDArray[np.complex128],
    reference2: NDArray[np.complex128],
    rows1: NDArray[np.complex128],
    rows2: NDArray[np.complex128],
) -> float:
    values = [
        _dual_similarity(reference1, reference2, row1, row2)
        for row1, row2 in zip(rows1, rows2, strict=True)
    ]
    return float(np.median(values)) if values else 0.0


def _match_landmarks(
    template1: NDArray[np.complex128],
    template2: NDArray[np.complex128],
    grid: NDArray[np.float64],
    rows1: NDArray[np.complex128],
    rows2: NDArray[np.complex128],
    landmarks: list[SpatialLandmark],
    config: SpatialLandmarkConfig,
) -> list[list[_LandmarkMatch | None]]:
    half_window = config.landmark_window_samples // 2
    shifts = _search_shifts(config)
    result: list[list[_LandmarkMatch | None]] = []
    for row1, row2 in zip(rows1, rows2, strict=True):
        row_matches: list[_LandmarkMatch | None] = []
        for landmark in landmarks:
            center = int(np.argmin(np.abs(grid - landmark.center_dtu)))
            start = center - half_window
            stop = center + half_window + 1
            reference1 = template1[start:stop]
            reference2 = template2[start:stop]
            target_indices = np.arange(start, stop, dtype=np.int64)
            scores = np.full(shifts.size, np.nan, dtype=np.float64)
            qualities = np.full(shifts.size, np.nan, dtype=np.float64)
            spatial_qualities = np.full(shifts.size, np.nan, dtype=np.float64)
            coherences = np.full(shifts.size, np.nan, dtype=np.float64)
            rank_one_scores = np.full(shifts.size, np.nan, dtype=np.float64)
            signal1 = _interpolate_uniform_shift_stack(
                row1,
                target_indices,
                shifts,
            )
            signal2 = _interpolate_uniform_shift_stack(
                row2,
                target_indices,
                shifts,
            )
            scores = _dual_similarity_scores(
                reference1,
                reference2,
                signal1,
                signal2,
            )
            spatial_qualities, coherences, rank_one_scores = _spatial_quality_scores(
                signal1,
                signal2,
            )
            qualities = scores * spatial_qualities * landmark.quality
            if not np.any(np.isfinite(scores)):
                row_matches.append(None)
                continue
            best = int(np.nanargmax(scores))
            fraction = _parabolic_peak_offset(scores, best)
            shift = shifts[best] + fraction * config.interpolation_step_dtu
            margin = _peak_margin(scores, best, config.interpolation_step_dtu)
            row_matches.append(
                _LandmarkMatch(
                    shift_dtu=float(shift),
                    score=float(scores[best]),
                    quality=float(qualities[best]),
                    margin=float(margin),
                    spatial_quality=float(spatial_qualities[best]),
                    coherence=float(coherences[best]),
                    rank_one_score=float(rank_one_scores[best]),
                )
            )
        result.append(row_matches)
    return result


def _match_full_cir(
    template: NDArray[np.complex128],
    grid: NDArray[np.float64],
    rows: NDArray[np.complex128],
    config: SpatialLandmarkConfig,
) -> _CirCorrelation:
    """Match the complete physical RF1 CIR against one fixed template."""
    shifts = _search_shifts(config)
    search_span = float(np.max(np.abs(shifts), initial=0.0))
    valid_window = (grid >= grid[0] + search_span) & (
        grid <= grid[-1] - search_span
    )
    template_power = np.abs(template) ** 2
    active_threshold = float(np.max(template_power, initial=0.0)) * 10.0 ** (
        -config.correlation_dynamic_range_db / 10.0
    )
    active_window = valid_window & (template_power >= active_threshold)
    sample_mask = (
        active_window if np.count_nonzero(active_window) >= 5 else valid_window
    )
    target_indices = np.flatnonzero(sample_mask)
    reference = template[sample_mask]
    count = rows.shape[0]
    output_shifts = np.full(count, np.nan, dtype=np.float64)
    scores = np.full(count, np.nan, dtype=np.float64)
    margins = np.full(count, np.nan, dtype=np.float64)
    if reference.size < 5:
        return _CirCorrelation(output_shifts, scores, margins)

    for row_index, row in enumerate(rows):
        signal = _interpolate_uniform_shift_stack(row, target_indices, shifts)
        candidate_scores = _cir_similarity_scores(reference, signal, config)
        if not np.any(np.isfinite(candidate_scores)):
            continue
        best = int(np.nanargmax(candidate_scores))
        output_shifts[row_index] = shifts[best] + (
            _parabolic_peak_offset(candidate_scores, best)
            * config.interpolation_step_dtu
        )
        scores[row_index] = candidate_scores[best]
        margins[row_index] = _peak_margin(
            candidate_scores,
            best,
            config.interpolation_step_dtu,
        )
    return _CirCorrelation(output_shifts, scores, margins)


@dataclass(frozen=True, slots=True)
class _Consensus:
    shift_dtu: NDArray[np.float64]
    quality: NDArray[np.float64]
    inlier_count: NDArray[np.int64]
    candidate_count: NDArray[np.int64]
    inlier_mask: NDArray[np.bool_]


def _consensus_timing(
    matches: list[list[_LandmarkMatch | None]],
    landmarks: list[SpatialLandmark],
    config: SpatialLandmarkConfig,
    *,
    track_reliability: NDArray[np.float64] | None = None,
) -> _Consensus:
    count = len(matches)
    shifts = np.full(count, np.nan, dtype=np.float64)
    quality = np.full(count, np.nan, dtype=np.float64)
    inlier_count = np.zeros(count, dtype=np.int64)
    candidate_count = np.zeros(count, dtype=np.int64)
    inlier_mask = np.zeros((count, len(landmarks)), dtype=bool)
    tolerance = _ns_to_dtu(config.consensus_half_range_ns)
    reliability = (
        np.ones(len(landmarks), dtype=np.float64)
        if track_reliability is None
        else track_reliability
    )
    for frame_index, frame_matches in enumerate(matches):
        candidates = [
            (landmark_index, match, landmark, reliability_value)
            for landmark_index, (match, landmark, reliability_value) in enumerate(
                zip(
                    frame_matches,
                    landmarks,
                    reliability,
                    strict=True,
                )
            )
            if match is not None
            and match.score >= config.minimum_match_score
            and match.quality > 0.0
            and reliability_value >= config.minimum_track_reliability
        ]
        candidate_count[frame_index] = len(candidates)
        if not candidates:
            continue
        values = np.asarray(
            [match.shift_dtu for _, match, _, _ in candidates],
            dtype=np.float64,
        )
        weights = np.asarray(
            [
                match.quality * reliability_value
                for _, match, _, reliability_value in candidates
            ],
            dtype=np.float64,
        )
        center = _weighted_median(values, weights)
        inliers = np.abs(values - center) <= tolerance
        if int(np.count_nonzero(inliers)) < config.minimum_consensus_inliers:
            continue
        final = _weighted_median(values[inliers], weights[inliers])
        shifts[frame_index] = final
        inlier_count[frame_index] = int(np.count_nonzero(inliers))
        for inlier, (landmark_index, _, _, _) in zip(
            inliers,
            candidates,
            strict=True,
        ):
            inlier_mask[frame_index, landmark_index] = bool(inlier)
        quality[frame_index] = float(
            np.sum(weights[inliers]) / max(np.sum(weights), np.finfo(float).eps)
        )
    return _Consensus(
        shifts,
        quality,
        inlier_count,
        candidate_count,
        inlier_mask,
    )


def _match_matrix(
    matches: list[list[_LandmarkMatch | None]],
    attribute: str,
) -> NDArray[np.float64]:
    """Return a frame-by-landmark diagnostic matrix with gaps as NaN."""
    landmark_count = len(matches[0]) if matches else 0
    values = np.full((len(matches), landmark_count), np.nan, dtype=np.float64)
    for frame_index, frame_matches in enumerate(matches):
        for landmark_index, match in enumerate(frame_matches):
            if match is not None:
                values[frame_index, landmark_index] = getattr(match, attribute)
    return values


def _track_reliability(
    matches: list[list[_LandmarkMatch | None]],
    consensus: _Consensus,
    config: SpatialLandmarkConfig,
) -> NDArray[np.float64]:
    """Score a landmark by its agreement with the common session shift.

    This offline pass rejects a patch that looks useful in the training template
    but repeatedly becomes an outlier later in the same recording.  The same
    inlier signal can become an EWMA when this estimator moves on-device.
    """
    landmark_count = len(matches[0]) if matches else 0
    reliability = np.zeros(landmark_count, dtype=np.float64)
    tolerance = _ns_to_dtu(config.consensus_half_range_ns)
    valid_consensus = np.isfinite(consensus.shift_dtu)
    if not np.any(valid_consensus):
        return reliability
    for landmark_index in range(landmark_count):
        qualities: list[float] = []
        inliers: list[bool] = []
        for frame_index, frame_matches in enumerate(matches):
            match = frame_matches[landmark_index]
            if (
                not valid_consensus[frame_index]
                or match is None
                or match.score < config.minimum_match_score
            ):
                continue
            qualities.append(match.score * match.spatial_quality)
            inliers.append(
                abs(match.shift_dtu - consensus.shift_dtu[frame_index])
                <= tolerance
            )
        if not qualities:
            continue
        persistence = float(np.mean(inliers))
        reliability[landmark_index] = np.sqrt(
            persistence * float(np.median(qualities))
        )
    return np.clip(reliability, 0.0, 1.0)


def _dual_similarity(
    reference1: NDArray[np.complex128],
    reference2: NDArray[np.complex128],
    signal1: NDArray[np.complex128],
    signal2: NDArray[np.complex128],
) -> float:
    channel_score = 0.5 * (
        _normalized_complex_similarity(reference1, signal1)
        + _normalized_complex_similarity(reference2, signal2)
    )
    reference_cross = reference2 * np.conjugate(reference1)
    signal_cross = signal2 * np.conjugate(signal1)
    spatial_score = _normalized_complex_similarity(reference_cross, signal_cross)
    return float(0.70 * channel_score + 0.30 * spatial_score)


def _dual_similarity_scores(
    reference1: NDArray[np.complex128],
    reference2: NDArray[np.complex128],
    signals1: NDArray[np.complex128],
    signals2: NDArray[np.complex128],
) -> NDArray[np.float64]:
    channel_score = 0.5 * (
        _normalized_complex_similarity_scores(reference1, signals1)
        + _normalized_complex_similarity_scores(reference2, signals2)
    )
    reference_cross = reference2 * np.conjugate(reference1)
    signal_cross = signals2 * np.conjugate(signals1)
    spatial_score = _normalized_complex_similarity_scores(
        reference_cross,
        signal_cross,
    )
    return 0.70 * channel_score + 0.30 * spatial_score


def _cir_similarity_scores(
    reference: NDArray[np.complex128],
    signals: NDArray[np.complex128],
    config: SpatialLandmarkConfig,
) -> NDArray[np.float64]:
    if config.correlation_mode == "amplitude":
        reference_feature = _amplitude_feature(
            reference,
            config.correlation_noise_percentile,
        )
        signal_features = np.asarray(
            [
                _amplitude_feature(signal, config.correlation_noise_percentile)
                for signal in signals
            ],
            dtype=np.float64,
        )
        return _normalized_real_similarity_scores(reference_feature, signal_features)
    return _normalized_complex_similarity_scores(reference, signals)


def _normalized_complex_similarity(
    reference: NDArray[np.complex128],
    signal: NDArray[np.complex128],
) -> float:
    numerator = abs(np.vdot(reference, signal))
    denominator = np.linalg.norm(reference) * np.linalg.norm(signal)
    if denominator <= np.finfo(float).eps:
        return 0.0
    return float(np.clip(numerator / denominator, 0.0, 1.0))


def _normalized_complex_similarity_scores(
    reference: NDArray[np.complex128],
    signals: NDArray[np.complex128],
) -> NDArray[np.float64]:
    numerator = np.abs(np.sum(np.conjugate(reference) * signals, axis=1))
    denominator = np.linalg.norm(reference) * np.linalg.norm(signals, axis=1)
    result = np.zeros(signals.shape[0], dtype=np.float64)
    valid = denominator > np.finfo(float).eps
    result[valid] = numerator[valid] / denominator[valid]
    return np.clip(result, 0.0, 1.0)


def _normalized_real_similarity_scores(
    reference: NDArray[np.float64],
    signals: NDArray[np.float64],
) -> NDArray[np.float64]:
    centered_reference = reference - np.mean(reference)
    centered_signals = signals - np.mean(signals, axis=1, keepdims=True)
    numerator = np.sum(centered_reference * centered_signals, axis=1)
    denominator = np.linalg.norm(centered_reference) * np.linalg.norm(
        centered_signals,
        axis=1,
    )
    result = np.zeros(signals.shape[0], dtype=np.float64)
    valid = denominator > np.finfo(float).eps
    result[valid] = numerator[valid] / denominator[valid]
    return np.clip(result, -1.0, 1.0)


def _amplitude_feature(
    values: NDArray[np.complex128],
    noise_percentile: float,
) -> NDArray[np.float64]:
    amplitude = np.abs(values)
    noise_floor = float(np.percentile(amplitude, noise_percentile))
    return np.sqrt(np.maximum(amplitude - noise_floor, 0.0))


def _spatial_quality(
    values1: NDArray[np.complex128],
    values2: NDArray[np.complex128],
) -> tuple[float, float, float]:
    power1 = float(np.vdot(values1, values1).real)
    power2 = float(np.vdot(values2, values2).real)
    cross = np.vdot(values1, values2)
    denominator = np.sqrt(power1 * power2)
    coherence = abs(cross) / denominator if denominator > 0.0 else 0.0
    trace = power1 + power2
    discriminant = np.sqrt((power1 - power2) ** 2 + 4.0 * abs(cross) ** 2)
    eigenvalue_1 = 0.5 * (trace + discriminant)
    eigenvalue_2 = 0.5 * (trace - discriminant)
    rank_one = (
        1.0 - eigenvalue_2 / eigenvalue_1 if eigenvalue_1 > 0.0 else 0.0
    )
    return float(np.sqrt(coherence * rank_one)), float(coherence), float(rank_one)


def _spatial_quality_scores(
    values1: NDArray[np.complex128],
    values2: NDArray[np.complex128],
) -> tuple[NDArray[np.float64], NDArray[np.float64], NDArray[np.float64]]:
    power1 = np.sum(np.abs(values1) ** 2, axis=1)
    power2 = np.sum(np.abs(values2) ** 2, axis=1)
    cross = np.sum(np.conjugate(values1) * values2, axis=1)
    denominator = np.sqrt(power1 * power2)
    coherence = np.divide(
        np.abs(cross),
        denominator,
        out=np.zeros_like(power1, dtype=np.float64),
        where=denominator > 0.0,
    )
    trace = power1 + power2
    discriminant = np.sqrt((power1 - power2) ** 2 + 4.0 * np.abs(cross) ** 2)
    eigenvalue_1 = 0.5 * (trace + discriminant)
    eigenvalue_2 = 0.5 * (trace - discriminant)
    rank_one = np.divide(
        eigenvalue_1 - eigenvalue_2,
        eigenvalue_1,
        out=np.zeros_like(eigenvalue_1, dtype=np.float64),
        where=eigenvalue_1 > 0.0,
    )
    return np.sqrt(coherence * rank_one), coherence, rank_one


def _search_shifts(config: SpatialLandmarkConfig) -> NDArray[np.float64]:
    span = int(np.ceil(_ns_to_dtu(config.search_half_range_ns)))
    step = config.interpolation_step_dtu
    return np.arange(-span, span + step, step, dtype=np.float64)


def _peak_margin(values: NDArray[np.float64], index: int, step_dtu: int) -> float:
    candidates = values.copy()
    exclusion = max(1, int(np.ceil(CIR_SAMPLE_DTU / step_dtu)))
    candidates[max(0, index - exclusion) : index + exclusion + 1] = np.nan
    if not np.any(np.isfinite(candidates)):
        return 0.0
    return float(values[index] - np.nanmax(candidates))


def _parabolic_peak_offset(values: NDArray[np.float64], index: int) -> float:
    if index == 0 or index == values.size - 1:
        return 0.0
    left, center, right = values[index - 1 : index + 2]
    denominator = left - 2.0 * center + right
    if not np.isfinite(denominator) or denominator >= 0.0:
        return 0.0
    return float(np.clip(0.5 * (left - right) / denominator, -1.0, 1.0))


def _fit_clock_model(
    receiver_timestamps: NDArray[np.integer],
    scheduled_timestamps: NDArray[np.int64],
    mode: str,
) -> _ClockModel:
    """Map TX schedule time onto the receiver DW timestamp domain.

    Every compared estimator shares this model.  Fitting it only to the native
    coarse RAWST prevents CIA first-path noise or a correlation-based estimate
    from being absorbed into a separate clock-rate fit.
    """
    schedule = _unwrap_timestamps(scheduled_timestamps)
    receiver = _unwrap_timestamps(receiver_timestamps)
    valid = np.isfinite(schedule) & np.isfinite(receiver)
    if not np.any(valid):
        raise ValueError("unable to fit a TX/RX clock model from DW RAWST")

    schedule_origin = float(schedule[valid][0])
    receiver_origin = float(receiver[valid][0])
    if mode == "median":
        return _ClockModel(schedule_origin, receiver_origin, 1.0)

    x = schedule[valid] - schedule_origin
    y = receiver[valid] - receiver_origin
    slope = _robust_slope(x, y)
    return _ClockModel(schedule_origin, receiver_origin, slope)


def _clock_compensated_errors(
    timestamps: NDArray[np.integer] | NDArray[np.floating],
    scheduled_timestamps: NDArray[np.int64],
    model: _ClockModel,
) -> NDArray[np.float64]:
    timestamp_unwrapped = _unwrap_timestamps(timestamps)
    scheduled_unwrapped = _unwrap_timestamps(scheduled_timestamps)
    predicted = model.receiver_origin_dtu + model.receiver_per_transmitter_dtu * (
        scheduled_unwrapped - model.schedule_origin_dtu
    )
    errors = timestamp_unwrapped - predicted
    valid = np.isfinite(errors)
    if np.any(valid):
        errors[valid] -= np.median(errors[valid])
    return errors


def _unwrap_timestamps(
    timestamps: NDArray[np.integer] | NDArray[np.floating],
) -> NDArray[np.float64]:
    values = np.asarray(timestamps, dtype=np.float64)
    unwrapped = np.full(values.size, np.nan, dtype=np.float64)
    previous_value = np.nan
    previous_unwrapped = np.nan
    half_modulus = DW_TIMESTAMP_MODULUS * 0.5
    for index, value in enumerate(values):
        if not np.isfinite(value):
            continue
        if not np.isfinite(previous_value):
            unwrapped[index] = value
        else:
            delta = (value - previous_value + half_modulus) % DW_TIMESTAMP_MODULUS
            delta -= half_modulus
            unwrapped[index] = previous_unwrapped + delta
        previous_value = value
        previous_unwrapped = unwrapped[index]
    return unwrapped


def _robust_slope(
    x: NDArray[np.float64],
    y: NDArray[np.float64],
) -> float:
    if x.size < 2 or np.ptp(x) <= 0.0:
        return 1.0
    weights = np.ones(x.size, dtype=np.float64)
    slope = 1.0
    for _ in range(4):
        weight_sum = float(np.sum(weights))
        mean_x = float(np.sum(weights * x) / weight_sum)
        mean_y = float(np.sum(weights * y) / weight_sum)
        centered_x = x - mean_x
        denominator = float(np.sum(weights * centered_x**2))
        if denominator <= np.finfo(float).eps:
            return 1.0
        slope = float(np.sum(weights * centered_x * (y - mean_y)) / denominator)
        intercept = mean_y - slope * mean_x
        residual = y - (slope * x + intercept)
        scale = max(1.4826 * np.median(np.abs(residual - np.median(residual))), 1.0)
        weights = np.minimum(1.0, 4.685 * scale / np.maximum(np.abs(residual), 1.0))
    return float(slope)


def _weighted_median(
    values: NDArray[np.float64],
    weights: NDArray[np.float64],
) -> float:
    order = np.argsort(values)
    sorted_values = values[order]
    sorted_weights = np.maximum(weights[order], 0.0)
    cumulative = np.cumsum(sorted_weights)
    if cumulative[-1] <= 0.0:
        return float(np.median(sorted_values))
    return float(sorted_values[np.searchsorted(cumulative, cumulative[-1] * 0.5)])


def _missing_sequence_count(sequence: NDArray[np.int64]) -> int:
    if sequence.size < 2:
        return 0
    steps = (
        sequence[1:].astype(np.uint64) - sequence[:-1].astype(np.uint64)
    ) & np.uint64(0xFFFFFFFF)
    valid = (steps > 0) & (steps < np.uint64(0x80000000))
    return int(np.sum(steps[valid] - 1, dtype=np.uint64))


def _transmit_interval_us(pairs: list[_Pair]) -> float | None:
    if len(pairs) < 2:
        return None
    delayed = np.asarray(
        [pair.schedule.delayed_time for pair in pairs],
        dtype=np.uint64,
    )
    deltas = (delayed[1:] - delayed[:-1]) & np.uint64(0xFFFFFFFF)
    valid = deltas > 0
    if not np.any(valid):
        return None
    return float(np.median(deltas[valid])) / (499.2e6 / 2.0) * 1e6


def _validate_config(config: SpatialLandmarkConfig) -> None:
    if config.template_frames < 2:
        raise ValueError("template_frames must be at least two")
    if config.maximum_landmarks < 1:
        raise ValueError("maximum_landmarks must be positive")
    if config.landmark_window_samples < 5 or config.landmark_window_samples % 2 == 0:
        raise ValueError(
            "landmark_window_samples must be an odd value of at least five"
        )
    if config.landmark_min_separation_samples < 1:
        raise ValueError("landmark_min_separation_samples must be positive")
    if config.search_half_range_ns <= 0.0:
        raise ValueError("search_half_range_ns must be positive")
    if config.interpolation_step_dtu < 1:
        raise ValueError("interpolation_step_dtu must be positive")
    if not 0.0 < config.minimum_match_score <= 1.0:
        raise ValueError("minimum_match_score must be in (0, 1]")
    if config.minimum_consensus_inliers < 1:
        raise ValueError("minimum_consensus_inliers must be positive")
    if config.consensus_half_range_ns <= 0.0:
        raise ValueError("consensus_half_range_ns must be positive")
    if not 0.0 <= config.minimum_rank_one_score <= 1.0:
        raise ValueError("minimum_rank_one_score must be in [0, 1]")
    if not 0.0 <= config.minimum_track_reliability <= 1.0:
        raise ValueError("minimum_track_reliability must be in [0, 1]")
    if config.correlation_mode not in {"complex", "amplitude"}:
        raise ValueError("correlation_mode must be 'complex' or 'amplitude'")
    if not 0.0 <= config.correlation_noise_percentile < 100.0:
        raise ValueError("correlation_noise_percentile must be in [0, 100)")
    if config.correlation_dynamic_range_db <= 0.0:
        raise ValueError("correlation_dynamic_range_db must be positive")
    if config.clock_drift_mode not in {"linear", "median"}:
        raise ValueError("clock_drift_mode must be 'linear' or 'median'")


def _power_db(values: NDArray[np.float64], reference: float) -> NDArray[np.float64]:
    return 10.0 * np.log10(np.maximum(values, 1.0) / reference)


def _ns_to_dtu(value_ns: float) -> float:
    return value_ns * 1e-9 / DW_TIME_UNIT_SECONDS


def _dtu_to_ns(
    values: NDArray[np.float64] | NDArray[np.integer],
) -> NDArray[np.float64]:
    return np.asarray(values, dtype=np.float64) * DW_TIME_UNIT_SECONDS * 1e9
