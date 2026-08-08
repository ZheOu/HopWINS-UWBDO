"""Static shared-clock timing analysis on a physical CIR time axis."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from numpy.typing import NDArray

from hopwins.analysis.cir import decode_i24_q24
from hopwins.analysis.statistics import DW_TIME_UNIT_SECONDS
from hopwins.capture.assembler import CirCapture

DW_TIMESTAMP_BITS = 40
DW_TIMESTAMP_MODULUS = 1 << DW_TIMESTAMP_BITS
CIR_SAMPLE_DTU = 64
DELAYED_TIME_DTU = 256
DELAYED_TIME_UNITS_PER_SECOND = 499.2e6 / 2.0
TX_FRAME_MAGIC = b"HWDO"
TX_FRAME_MAGIC_OFFSET = 9
TX_FRAME_SEQUENCE_OFFSET = 13
TX_FRAME_SCHEDULE_OFFSET = 17
TX_FRAME_MIN_LENGTH = 21


@dataclass(frozen=True, slots=True)
class CirCorrelationConfig:
    mode: str = "complex"
    template_frames: int = 50
    window_start_ns: float = -10.0
    window_stop_ns: float = 80.0
    search_half_range_ns: float = 10.0
    interpolation_step_dtu: int = 4
    noise_percentile: float = 20.0


@dataclass(frozen=True, slots=True)
class TransmitSchedule:
    sequence: int
    delayed_time: int

    @property
    def timestamp_dtu(self) -> int:
        return self.delayed_time << 8


@dataclass(frozen=True, slots=True)
class StaticTimingResult:
    source_capture_count: int
    skipped_capture_count: int
    rf_port: int
    sequence: NDArray[np.int64]
    capture_id: NDArray[np.int64]
    scheduled_time: NDArray[np.int64]
    scheduled_timestamp_dtu: NDArray[np.int64]
    rx_timestamp_dtu: NDArray[np.int64]
    raw_rx_timestamp_dtu: NDArray[np.int64]
    arrival_offset_dtu: NDArray[np.int64]
    common_time_dtu: NDArray[np.int64]
    common_time_ns: NDArray[np.float64]
    mean_power_db: NDArray[np.float64]
    median_power_db: NDArray[np.float64]
    power_p10_db: NDArray[np.float64]
    power_p90_db: NDArray[np.float64]
    coherent_power_db: NDArray[np.float64]
    first_frame_power_db: NDArray[np.float64]
    rx_error_dtu: NDArray[np.int64]
    rx_error_ns: NDArray[np.float64]
    fpi_equivalent_error_dtu: NDArray[np.float64]
    fpi_equivalent_error_ns: NDArray[np.float64]
    cir_match_error_dtu: NDArray[np.float64]
    cir_match_error_ns: NDArray[np.float64]
    cir_match_timestamp_dtu: NDArray[np.float64]
    cir_match_score: NDArray[np.float64]
    cir_match_peak_margin: NDArray[np.float64]
    cir_match_mode: str
    cir_match_template_frames: int
    strongest_path_ns: NDArray[np.float64]
    cir_similarity: NDArray[np.float64]
    first_path_index: NDArray[np.float64]
    rssi_dbm: NDArray[np.float64]
    cia_fpi_offset_dtu: NDArray[np.float64]
    arrival_origin_dtu: int
    missing_sequence_count: int
    transmit_interval_us: float | None

    @property
    def frame_count(self) -> int:
        return int(self.sequence.size)


@dataclass(frozen=True, slots=True)
class _FrameData:
    capture: CirCapture
    schedule: TransmitSchedule
    arrival_offset_dtu: int
    i_values: NDArray[np.int32]
    q_values: NDArray[np.int32]


@dataclass(frozen=True, slots=True)
class _CirMatchResult:
    error_dtu: NDArray[np.float64]
    score: NDArray[np.float64]
    peak_margin: NDArray[np.float64]
    template_frames: int


def parse_transmit_schedule(frame: bytes) -> TransmitSchedule | None:
    """Decode the HopWINS sequence and delayed-TX register value."""
    if (
        len(frame) < TX_FRAME_MIN_LENGTH
        or frame[
            TX_FRAME_MAGIC_OFFSET : TX_FRAME_MAGIC_OFFSET + len(TX_FRAME_MAGIC)
        ]
        != TX_FRAME_MAGIC
    ):
        return None
    return TransmitSchedule(
        sequence=int.from_bytes(
            frame[TX_FRAME_SEQUENCE_OFFSET:TX_FRAME_SCHEDULE_OFFSET],
            "little",
        ),
        delayed_time=int.from_bytes(
            frame[
                TX_FRAME_SCHEDULE_OFFSET : TX_FRAME_SCHEDULE_OFFSET + 4
            ],
            "little",
        ),
    )


def analyze_static_timing(
    captures: list[CirCapture],
    *,
    rf_port: int | None = None,
    correlation: CirCorrelationConfig | None = None,
) -> StaticTimingResult:
    """Aggregate valid captures without aligning CIRs to each frame's FPI."""
    correlation_config = correlation or CirCorrelationConfig()
    _validate_correlation_config(correlation_config)
    frames: list[_FrameData] = []
    for capture in captures:
        if rf_port is not None and capture.header.rf_port != rf_port:
            continue
        schedule = parse_transmit_schedule(capture.frame)
        if (
            schedule is None
            or not capture.has_cir
            or capture.header.bytes_per_sample != 6
            or capture.header.first_path_index_q10_6 == 0
        ):
            continue
        i_values, q_values = decode_i24_q24(capture.cir_bytes)
        if i_values.size == 0:
            continue
        arrival_offset = signed_timestamp_delta(
            capture.header.rx_timestamp,
            schedule.timestamp_dtu,
        )
        frames.append(
            _FrameData(
                capture,
                schedule,
                arrival_offset,
                i_values,
                q_values,
            )
        )

    if len(frames) < 2:
        raise ValueError(
            "static timing analysis requires at least two complete HWDO CIR captures"
        )
    selected_ports = {frame.capture.header.rf_port for frame in frames}
    if len(selected_ports) != 1:
        ports = ", ".join(str(port) for port in sorted(selected_ports))
        raise ValueError(
            f"capture contains multiple RF ports ({ports}); select one with rf_port"
        )

    arrival_offsets = np.asarray(
        [frame.arrival_offset_dtu for frame in frames],
        dtype=np.int64,
    )
    arrival_origin = int(np.rint(np.median(arrival_offsets)))
    rx_error_dtu = arrival_offsets - arrival_origin
    cia_fpi_calibration = _cia_fpi_calibration_dtu(frames)

    frame_axes = [
        _physical_cir_axis_dtu(
            frame,
            int(error),
            arrival_origin,
            cia_fpi_calibration,
        )
        for frame, error in zip(frames, rx_error_dtu, strict=True)
    ]
    common_time_dtu = _common_time_grid(frame_axes)
    complex_matrix = _interpolate_complex_cir(
        frames,
        frame_axes,
        common_time_dtu,
    )
    power_matrix = np.abs(complex_matrix) ** 2

    mean_power = np.mean(power_matrix, axis=0)
    median_power = np.median(power_matrix, axis=0)
    power_p10 = np.percentile(power_matrix, 10.0, axis=0)
    power_p90 = np.percentile(power_matrix, 90.0, axis=0)
    coherent_power = np.abs(_phase_aligned_mean(complex_matrix, mean_power)) ** 2
    first_frame_power = power_matrix[0]
    power_reference = max(float(np.max(mean_power, initial=0.0)), 1.0)

    strongest_path_dtu = common_time_dtu[
        np.argmax(power_matrix, axis=1)
    ]
    fpi_equivalent_error = _fpi_equivalent_errors(frames)
    cir_match = _match_cir_timing(
        frames,
        frame_axes,
        correlation_config,
    )
    sequences = np.asarray(
        [frame.schedule.sequence for frame in frames],
        dtype=np.int64,
    )
    scheduled_timestamps = np.asarray(
        [frame.schedule.timestamp_dtu for frame in frames],
        dtype=np.int64,
    )
    cir_match_timestamps = np.mod(
        scheduled_timestamps.astype(np.float64)
        + arrival_origin
        + cir_match.error_dtu,
        DW_TIMESTAMP_MODULUS,
    )

    return StaticTimingResult(
        source_capture_count=len(captures),
        skipped_capture_count=len(captures) - len(frames),
        rf_port=selected_ports.pop(),
        sequence=sequences,
        capture_id=np.asarray(
            [frame.capture.header.capture_id for frame in frames],
            dtype=np.int64,
        ),
        scheduled_time=np.asarray(
            [frame.schedule.delayed_time for frame in frames],
            dtype=np.int64,
        ),
        scheduled_timestamp_dtu=scheduled_timestamps,
        rx_timestamp_dtu=np.asarray(
            [frame.capture.header.rx_timestamp for frame in frames],
            dtype=np.int64,
        ),
        raw_rx_timestamp_dtu=np.asarray(
            [
                (
                    frame.capture.header.raw_rx_timestamp
                    if frame.capture.header.raw_rx_timestamp_valid
                    else -1
                )
                for frame in frames
            ],
            dtype=np.int64,
        ),
        arrival_offset_dtu=arrival_offsets,
        common_time_dtu=common_time_dtu,
        common_time_ns=_dtu_to_ns(common_time_dtu),
        mean_power_db=_power_db(mean_power, power_reference),
        median_power_db=_power_db(median_power, power_reference),
        power_p10_db=_power_db(power_p10, power_reference),
        power_p90_db=_power_db(power_p90, power_reference),
        coherent_power_db=_power_db(coherent_power, power_reference),
        first_frame_power_db=_power_db(first_frame_power, power_reference),
        rx_error_dtu=rx_error_dtu,
        rx_error_ns=_dtu_to_ns(rx_error_dtu),
        fpi_equivalent_error_dtu=fpi_equivalent_error,
        fpi_equivalent_error_ns=_dtu_to_ns(fpi_equivalent_error),
        cir_match_error_dtu=cir_match.error_dtu,
        cir_match_error_ns=_dtu_to_ns(cir_match.error_dtu),
        cir_match_timestamp_dtu=cir_match_timestamps,
        cir_match_score=cir_match.score,
        cir_match_peak_margin=cir_match.peak_margin,
        cir_match_mode=correlation_config.mode,
        cir_match_template_frames=cir_match.template_frames,
        strongest_path_ns=_dtu_to_ns(strongest_path_dtu),
        cir_similarity=_cir_similarity(power_matrix, mean_power),
        first_path_index=np.asarray(
            [frame.capture.header.first_path_index for frame in frames],
            dtype=np.float64,
        ),
        rssi_dbm=np.asarray(
            [
                (
                    frame.capture.header.rssi_dbm
                    if frame.capture.header.rssi_dbm is not None
                    else np.nan
                )
                for frame in frames
            ],
            dtype=np.float64,
        ),
        cia_fpi_offset_dtu=np.asarray(
            [
                (
                    frame.capture.header.cia_fpi_offset_dtu
                    if frame.capture.header.cia_fpi_offset_dtu is not None
                    else np.nan
                )
                for frame in frames
            ],
            dtype=np.float64,
        ),
        arrival_origin_dtu=arrival_origin,
        missing_sequence_count=_missing_sequence_count(sequences),
        transmit_interval_us=_transmit_interval_us(frames),
    )


def signed_timestamp_delta(timestamp: int, reference: int) -> int:
    """Return a signed difference in the modulo-40-bit DW timestamp domain."""
    value = (timestamp - reference) & (DW_TIMESTAMP_MODULUS - 1)
    if value & (1 << (DW_TIMESTAMP_BITS - 1)):
        return value - DW_TIMESTAMP_MODULUS
    return value


def _physical_cir_axis_dtu(
    frame: _FrameData,
    rx_error_dtu: int,
    arrival_origin_dtu: int,
    cia_fpi_calibration_dtu: int | None,
) -> NDArray[np.int64]:
    header = frame.capture.header
    accumulator_indices = (
        np.arange(frame.i_values.size, dtype=np.int64)
        + header.capture_sample_offset
    )
    if header.raw_rx_timestamp_valid and cia_fpi_calibration_dtu is not None:
        raw_offset = signed_timestamp_delta(
            header.raw_rx_timestamp,
            frame.schedule.timestamp_dtu,
        )
        return (
            raw_offset
            - arrival_origin_dtu
            + accumulator_indices * CIR_SAMPLE_DTU
            + cia_fpi_calibration_dtu
            - header.rx_antenna_delay
        )

    # Compatibility path for captures recorded before RX_RAWST was exported.
    return (
        rx_error_dtu
        + accumulator_indices * CIR_SAMPLE_DTU
        - header.first_path_index_q10_6
    )


def _cia_fpi_calibration_dtu(frames: list[_FrameData]) -> int | None:
    offsets = np.asarray(
        [
            (
                frame.capture.header.cia_fpi_offset_dtu
                if frame.capture.header.cia_fpi_offset_dtu is not None
                else np.nan
            )
            for frame in frames
        ],
        dtype=np.float64,
    )
    valid = np.isfinite(offsets)
    if not np.any(valid):
        return None
    return int(np.rint(np.median(offsets[valid])))


def _common_time_grid(
    frame_axes: list[NDArray[np.int64]],
) -> NDArray[np.int64]:
    overlap_start = max(int(axis[0]) for axis in frame_axes)
    overlap_stop = min(int(axis[-1]) for axis in frame_axes)
    grid_start = _ceil_to_multiple(overlap_start, CIR_SAMPLE_DTU)
    grid_stop = _floor_to_multiple(overlap_stop, CIR_SAMPLE_DTU)
    if grid_stop <= grid_start:
        raise ValueError("captured CIR windows do not overlap on the physical axis")
    return np.arange(
        grid_start,
        grid_stop + CIR_SAMPLE_DTU,
        CIR_SAMPLE_DTU,
        dtype=np.int64,
    )


def _interpolate_complex_cir(
    frames: list[_FrameData],
    frame_axes: list[NDArray[np.int64]],
    common_time_dtu: NDArray[np.int64],
) -> NDArray[np.complex128]:
    common = common_time_dtu.astype(np.float64)
    matrix = np.empty((len(frames), common_time_dtu.size), dtype=np.complex128)
    for row, (frame, axis) in enumerate(zip(frames, frame_axes, strict=True)):
        axis_float = axis.astype(np.float64)
        real = np.interp(common, axis_float, frame.i_values.astype(np.float64))
        imag = np.interp(common, axis_float, frame.q_values.astype(np.float64))
        matrix[row] = real + 1j * imag
    return matrix


def _match_cir_timing(
    frames: list[_FrameData],
    frame_axes: list[NDArray[np.int64]],
    config: CirCorrelationConfig,
) -> _CirMatchResult:
    count = len(frames)
    unavailable = np.full(count, np.nan, dtype=np.float64)
    if not all(frame.capture.header.raw_rx_timestamp_valid for frame in frames):
        return _CirMatchResult(unavailable, unavailable.copy(), unavailable.copy(), 0)

    step = config.interpolation_step_dtu
    search_steps = int(
        np.ceil(_ns_to_dtu(config.search_half_range_ns) / step)
    )
    search_span = search_steps * step
    available_start = max(int(axis[0]) for axis in frame_axes)
    available_stop = min(int(axis[-1]) for axis in frame_axes)
    window_start = max(
        int(np.rint(_ns_to_dtu(config.window_start_ns))),
        _ceil_to_multiple(available_start + search_span, step),
    )
    window_stop = min(
        int(np.rint(_ns_to_dtu(config.window_stop_ns))),
        _floor_to_multiple(available_stop - search_span, step),
    )
    if window_stop - window_start < 4 * CIR_SAMPLE_DTU:
        return _CirMatchResult(unavailable, unavailable.copy(), unavailable.copy(), 0)

    extended_grid = np.arange(
        window_start - search_span,
        window_stop + search_span + step,
        step,
        dtype=np.int64,
    )
    matrix = _interpolate_complex_cir(frames, frame_axes, extended_grid)
    template_count = min(config.template_frames, count)
    main_start = search_steps
    main_stop = matrix.shape[1] - search_steps
    training = matrix[:template_count, main_start:main_stop]

    if config.mode == "amplitude":
        feature_matrix = np.asarray(
            [
                _amplitude_feature(row, config.noise_percentile)
                for row in matrix
            ],
            dtype=np.float64,
        )
        template = np.median(
            feature_matrix[:template_count, main_start:main_stop],
            axis=0,
        )
        score_rows = (
            _normalized_real_correlation(row, template)
            for row in feature_matrix
        )
    else:
        template = _phase_aligned_mean(
            training,
            np.mean(np.abs(training) ** 2, axis=0),
        )
        score_rows = (
            _normalized_complex_correlation(row, template)
            for row in matrix
        )

    errors = np.full(count, np.nan, dtype=np.float64)
    scores = np.full(count, np.nan, dtype=np.float64)
    margins = np.full(count, np.nan, dtype=np.float64)
    exclusion = max(1, int(np.ceil(CIR_SAMPLE_DTU / step)))
    for index, correlation_scores in enumerate(score_rows):
        finite = np.isfinite(correlation_scores)
        if not np.any(finite):
            continue
        best = int(np.nanargmax(correlation_scores))
        scores[index] = correlation_scores[best]
        fraction = _parabolic_peak_offset(correlation_scores, best)
        errors[index] = (best - search_steps + fraction) * step

        candidates = correlation_scores.copy()
        candidates[
            max(0, best - exclusion) : min(
                candidates.size,
                best + exclusion + 1,
            )
        ] = np.nan
        if np.any(np.isfinite(candidates)):
            margins[index] = scores[index] - float(np.nanmax(candidates))

    valid = np.isfinite(errors)
    if np.any(valid):
        errors[valid] -= np.median(errors[valid])
    return _CirMatchResult(errors, scores, margins, template_count)


def _amplitude_feature(
    values: NDArray[np.complex128],
    noise_percentile: float,
) -> NDArray[np.float64]:
    amplitude = np.abs(values)
    noise_floor = float(np.percentile(amplitude, noise_percentile))
    # Compression prevents one strong NLoS reflection from suppressing the
    # timing information carried by the rest of the stable multipath profile.
    return np.sqrt(np.maximum(amplitude - noise_floor, 0.0))


def _normalized_real_correlation(
    signal: NDArray[np.float64],
    template: NDArray[np.float64],
) -> NDArray[np.float64]:
    centered_template = template - np.mean(template)
    template_energy = float(np.sum(centered_template**2))
    if template_energy <= 0.0:
        return np.full(signal.size - template.size + 1, np.nan)

    numerator = _fft_valid_correlation(signal, centered_template).real
    window_sum = _moving_sum(signal, template.size)
    window_energy = (
        _moving_sum(signal**2, template.size)
        - window_sum**2 / template.size
    )
    denominator = np.sqrt(
        template_energy * np.maximum(window_energy, 0.0)
    )
    scores = np.full(numerator.size, np.nan, dtype=np.float64)
    valid = denominator > np.finfo(np.float64).eps
    scores[valid] = numerator[valid] / denominator[valid]
    return np.clip(scores, -1.0, 1.0)


def _normalized_complex_correlation(
    signal: NDArray[np.complex128],
    template: NDArray[np.complex128],
) -> NDArray[np.float64]:
    centered_template = template - np.mean(template)
    template_energy = float(np.sum(np.abs(centered_template) ** 2))
    if template_energy <= 0.0:
        return np.full(signal.size - template.size + 1, np.nan)

    numerator = np.abs(_fft_valid_correlation(signal, centered_template))
    window_sum = _moving_sum(signal, template.size)
    window_energy = (
        _moving_sum(np.abs(signal) ** 2, template.size)
        - np.abs(window_sum) ** 2 / template.size
    ).real
    denominator = np.sqrt(
        template_energy * np.maximum(window_energy, 0.0)
    )
    scores = np.full(numerator.size, np.nan, dtype=np.float64)
    valid = denominator > np.finfo(np.float64).eps
    scores[valid] = numerator[valid] / denominator[valid]
    return np.clip(scores, 0.0, 1.0)


def _fft_valid_correlation(
    signal: NDArray[np.generic],
    template: NDArray[np.generic],
) -> NDArray[np.complex128]:
    output_size = signal.size + template.size - 1
    fft_size = 1 << (output_size - 1).bit_length()
    convolution = np.fft.ifft(
        np.fft.fft(signal, fft_size)
        * np.fft.fft(np.conjugate(template[::-1]), fft_size)
    )
    return convolution[template.size - 1 : signal.size]


def _moving_sum(
    values: NDArray[np.generic],
    window: int,
) -> NDArray[np.generic]:
    prefix = np.concatenate(
        (
            np.zeros(1, dtype=values.dtype),
            np.cumsum(values),
        )
    )
    return prefix[window:] - prefix[:-window]


def _parabolic_peak_offset(
    scores: NDArray[np.float64],
    index: int,
) -> float:
    if index == 0 or index == scores.size - 1:
        return 0.0
    left = scores[index - 1]
    center = scores[index]
    right = scores[index + 1]
    if not np.all(np.isfinite((left, center, right))):
        return 0.0
    denominator = left - 2.0 * center + right
    if denominator >= 0.0 or abs(denominator) < np.finfo(np.float64).eps:
        return 0.0
    return float(np.clip(0.5 * (left - right) / denominator, -1.0, 1.0))


def _phase_aligned_mean(
    matrix: NDArray[np.complex128],
    mean_power: NDArray[np.float64],
) -> NDArray[np.complex128]:
    threshold = max(float(np.max(mean_power, initial=0.0)) * 1e-3, 1.0)
    signal_mask = mean_power >= threshold
    if not np.any(signal_mask):
        return np.mean(matrix, axis=0)

    energies = np.sum(np.abs(matrix[:, signal_mask]) ** 2, axis=1)
    reference_index = int(np.argsort(energies)[len(energies) // 2])
    reference = matrix[reference_index].copy()
    aligned = matrix.copy()

    for _ in range(2):
        for row in range(matrix.shape[0]):
            correlation = np.vdot(reference[signal_mask], matrix[row, signal_mask])
            phase = np.angle(correlation) if correlation != 0.0 else 0.0
            aligned[row] = matrix[row] * np.exp(-1j * phase)
        reference = np.mean(aligned, axis=0)
    return reference


def _fpi_equivalent_errors(
    frames: list[_FrameData],
) -> NDArray[np.float64]:
    values = np.full(len(frames), np.nan, dtype=np.float64)
    for index, frame in enumerate(frames):
        header = frame.capture.header
        if not header.raw_rx_timestamp_valid:
            continue
        values[index] = signed_timestamp_delta(
            (
                header.raw_rx_timestamp
                + header.first_path_index_q10_6
                - header.rx_antenna_delay
            )
            & (DW_TIMESTAMP_MODULUS - 1),
            frame.schedule.timestamp_dtu,
        )
    valid = np.isfinite(values)
    if np.any(valid):
        values[valid] -= np.rint(np.median(values[valid]))
    return values


def _cir_similarity(
    power_matrix: NDArray[np.float64],
    mean_power: NDArray[np.float64],
) -> NDArray[np.float64]:
    template = mean_power - np.mean(mean_power)
    template_norm = np.linalg.norm(template)
    result = np.full(power_matrix.shape[0], np.nan, dtype=np.float64)
    if template_norm == 0.0:
        return result
    for index, power in enumerate(power_matrix):
        centered = power - np.mean(power)
        denominator = np.linalg.norm(centered) * template_norm
        if denominator != 0.0:
            result[index] = float(np.dot(centered, template) / denominator)
    return result


def _missing_sequence_count(sequence: NDArray[np.int64]) -> int:
    if sequence.size < 2:
        return 0
    steps = (
        sequence[1:].astype(np.uint64) - sequence[:-1].astype(np.uint64)
    ) & np.uint64(0xFFFFFFFF)
    valid = (steps > 0) & (steps < np.uint64(0x80000000))
    return int(np.sum(steps[valid] - 1, dtype=np.uint64))


def _transmit_interval_us(frames: list[_FrameData]) -> float | None:
    intervals: list[float] = []
    for previous, current in zip(frames, frames[1:], strict=False):
        sequence_step = (
            current.schedule.sequence - previous.schedule.sequence
        ) & 0xFFFFFFFF
        if sequence_step == 0 or sequence_step >= 0x80000000:
            continue
        delayed_step = (
            current.schedule.delayed_time - previous.schedule.delayed_time
        ) & 0xFFFFFFFF
        intervals.append(delayed_step / sequence_step)
    if not intervals:
        return None
    return float(np.median(intervals) / DELAYED_TIME_UNITS_PER_SECOND * 1e6)


def _power_db(
    power: NDArray[np.float64],
    reference: float,
) -> NDArray[np.float64]:
    floor = max(reference * 1e-12, np.finfo(np.float64).tiny)
    return 10.0 * np.log10(np.maximum(power, floor) / reference)


def _dtu_to_ns(values: NDArray[np.generic]) -> NDArray[np.float64]:
    return values.astype(np.float64) * DW_TIME_UNIT_SECONDS * 1e9


def _ns_to_dtu(value_ns: float) -> float:
    return value_ns / (DW_TIME_UNIT_SECONDS * 1e9)


def _validate_correlation_config(config: CirCorrelationConfig) -> None:
    if config.mode not in ("amplitude", "complex"):
        raise ValueError("CIR correlation mode must be amplitude or complex")
    if config.template_frames < 2:
        raise ValueError("CIR correlation requires at least two template frames")
    if config.window_stop_ns <= config.window_start_ns:
        raise ValueError("CIR correlation window stop must follow its start")
    if config.search_half_range_ns <= 0.0:
        raise ValueError("CIR correlation search range must be positive")
    if (
        config.interpolation_step_dtu <= 0
        or CIR_SAMPLE_DTU % config.interpolation_step_dtu != 0
    ):
        raise ValueError(
            "CIR correlation interpolation step must divide 64 DTU"
        )
    if not 0.0 <= config.noise_percentile < 50.0:
        raise ValueError("CIR correlation noise percentile must be in [0, 50)")


def _ceil_to_multiple(value: int, multiple: int) -> int:
    return -((-value) // multiple) * multiple


def _floor_to_multiple(value: int, multiple: int) -> int:
    return (value // multiple) * multiple
