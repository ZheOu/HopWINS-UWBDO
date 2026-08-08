"""Offline dual-antenna spatial-landmark timing analysis."""

from __future__ import annotations

import csv
from pathlib import Path
from typing import TYPE_CHECKING

import numpy as np

from hopwins.analysis.spatial_landmarks import (
    SpatialLandmarkConfig,
    SpatialLandmarkResult,
    analyze_spatial_landmarks,
)
from hopwins.analysis.statistics import DW_TIME_UNIT_SECONDS
from hopwins.capture.reader import CaptureFileReader

if TYPE_CHECKING:
    from hopwins.tasks.registry import TaskContext


def run(
    path: str | Path,
    *,
    limit: int = 0,
    histogram_bin_dtu: int = 1,
    csv_path: str | Path | None = None,
    show: bool = True,
    config: SpatialLandmarkConfig | None = None,
) -> int:
    source_path = Path(path)
    captures = list(CaptureFileReader(source_path))
    if limit > 0:
        captures = captures[:limit]
    result = analyze_spatial_landmarks(captures, config=config)
    _print_summary(source_path, result)

    if csv_path is not None:
        output_path = Path(csv_path)
        landmark_output_path = _write_csv(output_path, result)
        print(f"frame statistics: {output_path}")
        print(f"landmark diagnostics: {landmark_output_path}")

    if not show:
        return 0
    from hopwins.ui.spatial_timing_window import run_spatial_timing_window

    return run_spatial_timing_window(
        result,
        source_path,
        histogram_bin_dtu=histogram_bin_dtu,
    )


def run_configured(context: TaskContext) -> int:
    configured_path = context.config.task_text(context.task_name, "path")
    path = (
        context.config.latest_capture_path()
        if configured_path in ("", "latest")
        else context.config.resolve_path(configured_path)
    )
    export_csv = context.config.task_bool(
        context.task_name,
        "export_csv",
        fallback=True,
    )
    csv_setting = context.config.task_text(context.task_name, "csv_path")
    csv_path = (
        context.config.resolve_path(csv_setting)
        if export_csv and csv_setting
        else path.with_suffix(".spatial-timing.csv")
        if export_csv
        else None
    )
    return run(
        path,
        limit=context.config.task_int(context.task_name, "limit", fallback=0),
        histogram_bin_dtu=context.config.task_int(
            context.task_name,
            "histogram_bin_dtu",
            fallback=1,
        ),
        csv_path=csv_path,
        show=context.config.task_bool(context.task_name, "show", fallback=True),
        config=SpatialLandmarkConfig(
            template_frames=context.config.task_int(
                context.task_name,
                "template_frames",
                fallback=50,
            ),
            maximum_landmarks=context.config.task_int(
                context.task_name,
                "maximum_landmarks",
                fallback=8,
            ),
            landmark_window_samples=context.config.task_int(
                context.task_name,
                "landmark_window_samples",
                fallback=13,
            ),
            landmark_min_separation_samples=context.config.task_int(
                context.task_name,
                "landmark_min_separation_samples",
                fallback=10,
            ),
            search_half_range_ns=context.config.task_float(
                context.task_name,
                "search_half_range_ns",
                fallback=10.0,
            ),
            interpolation_step_dtu=context.config.task_int(
                context.task_name,
                "interpolation_step_dtu",
                fallback=4,
            ),
            minimum_match_score=context.config.task_float(
                context.task_name,
                "minimum_match_score",
                fallback=0.60,
            ),
            minimum_consensus_inliers=context.config.task_int(
                context.task_name,
                "minimum_consensus_inliers",
                fallback=2,
            ),
            consensus_half_range_ns=context.config.task_float(
                context.task_name,
                "consensus_half_range_ns",
                fallback=0.75,
            ),
            minimum_rank_one_score=context.config.task_float(
                context.task_name,
                "minimum_rank_one_score",
                fallback=0.35,
            ),
            minimum_track_reliability=context.config.task_float(
                context.task_name,
                "minimum_track_reliability",
                fallback=0.55,
            ),
            correlation_mode=context.config.task_text(
                context.task_name,
                "correlation_mode",
                fallback="complex",
            ),
            correlation_noise_percentile=context.config.task_float(
                context.task_name,
                "correlation_noise_percentile",
                fallback=20.0,
            ),
            correlation_dynamic_range_db=context.config.task_float(
                context.task_name,
                "correlation_dynamic_range_db",
                fallback=30.0,
            ),
            clock_drift_mode=context.config.task_text(
                context.task_name,
                "clock_drift_mode",
                fallback="linear",
            ),
        ),
    )


def _print_summary(path: Path, result: SpatialLandmarkResult) -> None:
    print(f"source: {path}")
    print(
        f"pairs: {result.frame_count} used, {result.skipped_capture_count} captures "
        f"skipped, {result.missing_sequence_count} missing sequences, "
        f"RF{result.rf1_port}/RF{result.rf2_port}"
    )
    print(
        f"landmarks: {len(result.landmarks)}, "
        f"consensus median inliers={np.median(result.landmark_inlier_count):.0f}"
    )
    print(
        f"clock model: {result.clock_drift_mode}, "
        f"RX clock error={result.clock_frequency_error_ppb:.3f} ppb"
    )
    _print_timestamp_summary("DW RXTS", result.rx_error_ns)
    _print_timestamp_summary("Full CIR correlation", result.cir_correlation_error_ns)
    _print_timestamp_summary("Spatial landmark", result.landmark_error_ns)
    correlation_scores = result.cir_correlation_score[
        np.isfinite(result.cir_correlation_score)
    ]
    correlation_margins = result.cir_correlation_peak_margin[
        np.isfinite(result.cir_correlation_peak_margin)
    ]
    if correlation_scores.size:
        print(
            "full CIR correlation: "
            f"score median/min={np.median(correlation_scores):.4f}/"
            f"{np.min(correlation_scores):.4f}"
        )
    if correlation_margins.size:
        print(
            "full CIR correlation peak margin: "
            f"median/min={np.median(correlation_margins):.4f}/"
            f"{np.min(correlation_margins):.4f}"
        )
    valid_quality = result.landmark_quality[np.isfinite(result.landmark_quality)]
    if valid_quality.size:
        print(
            "landmark quality: "
            f"median={np.median(valid_quality):.4f}, min={np.min(valid_quality):.4f}"
        )
    track_reliability = result.landmark_track_reliability
    if track_reliability.size:
        print(
            "landmark track reliability: "
            f"median={np.median(track_reliability):.4f}, "
            f"min={np.min(track_reliability):.4f}"
        )
    pdoa = result.hardware_pdoa_radians[
        np.isfinite(result.hardware_pdoa_radians)
    ]
    if pdoa.size:
        print(
            "CIA PDoA diagnostic: "
            f"median={np.median(pdoa):.4f} rad, std={np.std(pdoa):.4f} rad"
        )
    if result.transmit_interval_us is not None:
        print(f"TX scheduled interval: {result.transmit_interval_us:.6f} us")


def _print_timestamp_summary(name: str, values_ns: np.ndarray) -> None:
    valid = values_ns[np.isfinite(values_ns)]
    if not valid.size:
        print(f"{name}: unavailable")
        return
    print(
        f"{name}: frames={valid.size}, std={np.std(valid) * 1e3:.3f} ps, "
        f"MAD={np.median(np.abs(valid - np.median(valid))) * 1e3:.3f} ps, "
        f"peak-to-peak={np.ptp(valid) * 1e3:.3f} ps"
    )


def _write_csv(path: Path, result: SpatialLandmarkResult) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = (
        "capture_id",
        "sequence",
        "scheduled_timestamp_dtu",
        "rx_timestamp_dtu",
        "rawst_rf1_fpi_timestamp_dtu",
        "cir_correlation_timestamp_dtu",
        "spatial_landmark_timestamp_dtu",
        "rxts_error_dtu",
        "rxts_error_ps",
        "rawst_rf1_fpi_error_dtu",
        "rawst_rf1_fpi_error_ps",
        "cir_correlation_error_dtu",
        "cir_correlation_error_ps",
        "spatial_landmark_error_dtu",
        "spatial_landmark_error_ps",
        "cir_correlation_score",
        "cir_correlation_peak_margin",
        "clock_frequency_error_ppb",
        "clock_drift_mode",
        "landmark_vote_dtu",
        "landmark_quality",
        "landmark_inliers",
        "landmark_candidates",
        "landmark_track_reliability_median",
        "hardware_pdoa_radians",
        "hardware_tdoa_dtu",
    )
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for index in range(result.frame_count):
            writer.writerow(
                {
                    "capture_id": int(result.capture_id[index]),
                    "sequence": int(result.sequence[index]),
                    "scheduled_timestamp_dtu": int(
                        result.scheduled_timestamp_dtu[index]
                    ),
                    "rx_timestamp_dtu": int(result.rx_timestamp_dtu[index]),
                    "rawst_rf1_fpi_timestamp_dtu": int(
                        result.fpi_timestamp_dtu[index]
                    ),
                    "cir_correlation_timestamp_dtu": _optional_number(
                        result.cir_correlation_timestamp_dtu[index]
                    ),
                    "spatial_landmark_timestamp_dtu": _optional_number(
                        result.landmark_timestamp_dtu[index]
                    ),
                    "rxts_error_dtu": result.rx_error_dtu[index],
                    "rxts_error_ps": result.rx_error_ns[index] * 1e3,
                    "rawst_rf1_fpi_error_dtu": result.fpi_error_dtu[index],
                    "rawst_rf1_fpi_error_ps": result.fpi_error_ns[index] * 1e3,
                    "cir_correlation_error_dtu": _optional_number(
                        result.cir_correlation_error_dtu[index]
                    ),
                    "cir_correlation_error_ps": _optional_number(
                        result.cir_correlation_error_ns[index] * 1e3
                    ),
                    "spatial_landmark_error_dtu": _optional_number(
                        result.landmark_error_dtu[index]
                    ),
                    "spatial_landmark_error_ps": _optional_number(
                        result.landmark_error_ns[index] * 1e3
                    ),
                    "cir_correlation_score": _optional_number(
                        result.cir_correlation_score[index]
                    ),
                    "cir_correlation_peak_margin": _optional_number(
                        result.cir_correlation_peak_margin[index]
                    ),
                    "clock_frequency_error_ppb": result.clock_frequency_error_ppb,
                    "clock_drift_mode": result.clock_drift_mode,
                    "landmark_vote_dtu": _optional_number(
                        result.landmark_vote_dtu[index]
                    ),
                    "landmark_quality": _optional_number(
                        result.landmark_quality[index]
                    ),
                    "landmark_inliers": int(result.landmark_inlier_count[index]),
                    "landmark_candidates": int(
                        result.landmark_candidate_count[index]
                    ),
                    "landmark_track_reliability_median": _optional_number(
                        float(np.median(result.landmark_track_reliability))
                        if result.landmark_track_reliability.size
                        else np.nan
                    ),
                    "hardware_pdoa_radians": _optional_number(
                        result.hardware_pdoa_radians[index]
                    ),
                    "hardware_tdoa_dtu": _optional_number(
                        result.hardware_tdoa_dtu[index]
                    ),
                }
            )
    landmark_path = path.with_name(f"{path.stem}.landmarks.csv")
    _write_landmark_csv(landmark_path, result)
    return landmark_path


def _write_landmark_csv(path: Path, result: SpatialLandmarkResult) -> None:
    fieldnames = (
        "capture_id",
        "sequence",
        "landmark_index",
        "reference_offset_ns",
        "shift_dtu",
        "shift_ns",
        "match_score",
        "match_quality",
        "match_margin",
        "spatial_quality",
        "coherence",
        "rank_one_score",
        "track_reliability",
        "in_consensus",
        "frame_vote_dtu",
        "frame_consensus_quality",
    )
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for frame_index in range(result.frame_count):
            for landmark_index, landmark in enumerate(result.landmarks):
                shift_dtu = result.landmark_shift_dtu[frame_index, landmark_index]
                writer.writerow(
                    {
                        "capture_id": int(result.capture_id[frame_index]),
                        "sequence": int(result.sequence[frame_index]),
                        "landmark_index": landmark_index,
                        "reference_offset_ns": _dtu_to_ns(
                            landmark.center_dtu - result.reference_landmark_dtu
                        ),
                        "shift_dtu": _optional_number(shift_dtu),
                        "shift_ns": _optional_number(_dtu_to_ns(shift_dtu)),
                        "match_score": _optional_number(
                            result.landmark_match_score[frame_index, landmark_index]
                        ),
                        "match_quality": _optional_number(
                            result.landmark_match_quality[frame_index, landmark_index]
                        ),
                        "match_margin": _optional_number(
                            result.landmark_match_margin[frame_index, landmark_index]
                        ),
                        "spatial_quality": _optional_number(
                            result.landmark_spatial_quality[
                                frame_index,
                                landmark_index,
                            ]
                        ),
                        "coherence": _optional_number(
                            result.landmark_coherence[frame_index, landmark_index]
                        ),
                        "rank_one_score": _optional_number(
                            result.landmark_rank_one_score[
                                frame_index,
                                landmark_index,
                            ]
                        ),
                        "track_reliability": _optional_number(
                            result.landmark_track_reliability[landmark_index]
                        ),
                        "in_consensus": int(
                            result.landmark_inlier_mask[
                                frame_index,
                                landmark_index,
                            ]
                        ),
                        "frame_vote_dtu": _optional_number(
                            result.landmark_vote_dtu[frame_index]
                        ),
                        "frame_consensus_quality": _optional_number(
                            result.landmark_quality[frame_index]
                        ),
                    }
                )


def _optional_number(value: float) -> float | str:
    return float(value) if np.isfinite(value) else ""


def _dtu_to_ns(value_dtu: float) -> float:
    return float(value_dtu) * DW_TIME_UNIT_SECONDS * 1e9
