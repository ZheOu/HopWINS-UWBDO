"""Offline shared-clock CIR and RX timestamp analysis."""

from __future__ import annotations

import csv
from pathlib import Path
from typing import TYPE_CHECKING

import numpy as np

from hopwins.analysis.static_timing import (
    CirCorrelationConfig,
    StaticTimingResult,
    analyze_static_timing,
)
from hopwins.capture.reader import CaptureFileReader

if TYPE_CHECKING:
    from hopwins.tasks.registry import TaskContext


def run(
    path: str | Path,
    *,
    limit: int = 0,
    histogram_bin_dtu: int = 1,
    histogram_min_half_range_ns: float = 10.0,
    view_pre_ns: float = 30.0,
    view_post_ns: float = 100.0,
    csv_path: str | Path | None = None,
    show: bool = True,
    rf_port: int | None = None,
    correlation: CirCorrelationConfig | None = None,
) -> int:
    source_path = Path(path)
    captures = list(CaptureFileReader(source_path))
    if limit > 0:
        captures = captures[:limit]
    result = analyze_static_timing(
        captures,
        rf_port=rf_port,
        correlation=correlation,
    )
    _print_summary(source_path, result)

    if csv_path is not None:
        output_path = Path(csv_path)
        _write_csv(output_path, result)
        print(f"frame statistics: {output_path}")

    if not show:
        return 0
    from hopwins.ui.static_timing_window import run_static_timing_window

    return run_static_timing_window(
        result,
        source_path,
        histogram_bin_dtu=histogram_bin_dtu,
        histogram_min_half_range_ns=histogram_min_half_range_ns,
        view_pre_ns=view_pre_ns,
        view_post_ns=view_post_ns,
    )


def run_configured(context: TaskContext) -> int:
    configured_path = context.config.task_text(context.task_name, "path")
    path = (
        context.config.latest_capture_path()
        if configured_path in ("", "latest")
        else context.config.resolve_path(configured_path)
    )
    csv_setting = context.config.task_text(context.task_name, "csv_path")
    export_csv = context.config.task_bool(
        context.task_name,
        "export_csv",
        fallback=True,
    )
    csv_path: Path | None = None
    if export_csv:
        csv_path = (
            context.config.resolve_path(csv_setting)
            if csv_setting
            else path.with_suffix(".timing.csv")
        )
    rf_port_setting = context.config.task_text(context.task_name, "rf_port")
    rf_port = int(rf_port_setting, 0) if rf_port_setting else None

    return run(
        path,
        limit=context.config.task_int(context.task_name, "limit", fallback=0),
        histogram_bin_dtu=context.config.task_int(
            context.task_name,
            "histogram_bin_dtu",
            fallback=1,
        ),
        histogram_min_half_range_ns=context.config.task_float(
            context.task_name,
            "histogram_min_half_range_ns",
            fallback=10.0,
        ),
        view_pre_ns=context.config.task_float(
            context.task_name,
            "view_pre_ns",
            fallback=30.0,
        ),
        view_post_ns=context.config.task_float(
            context.task_name,
            "view_post_ns",
            fallback=100.0,
        ),
        csv_path=csv_path,
        show=context.config.task_bool(
            context.task_name,
            "show",
            fallback=True,
        ),
        rf_port=rf_port,
        correlation=CirCorrelationConfig(
            mode=context.config.task_text(
                context.task_name,
                "correlation_mode",
                fallback="complex",
            ),
            template_frames=context.config.task_int(
                context.task_name,
                "correlation_template_frames",
                fallback=50,
            ),
            window_start_ns=context.config.task_float(
                context.task_name,
                "correlation_window_start_ns",
                fallback=-10.0,
            ),
            window_stop_ns=context.config.task_float(
                context.task_name,
                "correlation_window_stop_ns",
                fallback=80.0,
            ),
            search_half_range_ns=context.config.task_float(
                context.task_name,
                "correlation_search_half_range_ns",
                fallback=10.0,
            ),
            interpolation_step_dtu=context.config.task_int(
                context.task_name,
                "correlation_interpolation_step_dtu",
                fallback=4,
            ),
            noise_percentile=context.config.task_float(
                context.task_name,
                "correlation_noise_percentile",
                fallback=20.0,
            ),
        ),
    )


def _print_summary(path: Path, result: StaticTimingResult) -> None:
    similarity = result.cir_similarity[np.isfinite(result.cir_similarity)]
    fpi_valid = np.isfinite(result.fpi_equivalent_error_dtu)
    rx_fpi_difference = (
        result.rx_error_dtu[fpi_valid]
        - result.fpi_equivalent_error_dtu[fpi_valid]
    )
    interval = (
        f"{result.transmit_interval_us:.6f} us"
        if result.transmit_interval_us is not None
        else "unavailable"
    )
    print(f"source: {path}")
    print(
        f"frames: {result.frame_count} used, "
        f"{result.skipped_capture_count} skipped, "
        f"{result.missing_sequence_count} missing sequences, "
        f"RF{result.rf_port}"
    )
    print(f"TX scheduled interval: {interval}")
    print(
        f"RXTS error: mean={np.mean(result.rx_error_ns) * 1e3:.3f} ps, "
        f"std={np.std(result.rx_error_ns) * 1e3:.3f} ps, "
        f"peak-to-peak={np.ptp(result.rx_error_ns) * 1e3:.3f} ps"
    )
    if similarity.size:
        print(
            f"CIR similarity: median={np.median(similarity):.6f}, "
            f"minimum={np.min(similarity):.6f}"
        )
    if rx_fpi_difference.size:
        print(
            "RXTS - (RAWST + FPI) centered residual: "
            f"std={np.std(rx_fpi_difference):.3f} DTU, "
            f"span={np.ptp(rx_fpi_difference):.3f} DTU"
        )
    match_valid = np.isfinite(result.cir_match_error_ns)
    if np.any(match_valid):
        matched = result.cir_match_error_ns[match_valid]
        scores = result.cir_match_score[match_valid]
        margins = result.cir_match_peak_margin[
            np.isfinite(result.cir_match_peak_margin)
        ]
        print(
            f"CIR match ({result.cir_match_mode}, "
            f"template={result.cir_match_template_frames}): "
            f"std={np.std(matched) * 1e3:.3f} ps, "
            f"peak-to-peak={np.ptp(matched) * 1e3:.3f} ps, "
            f"score median/min={np.median(scores):.5f}/{np.min(scores):.5f}"
        )
        if margins.size:
            print(
                "CIR match peak margin: "
                f"median/min={np.median(margins):.5f}/{np.min(margins):.5f}"
            )


def _write_csv(path: Path, result: StaticTimingResult) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = (
        "capture_id",
        "sequence",
        "scheduled_time_dly",
        "scheduled_timestamp_dtu",
        "rx_timestamp_dtu",
        "raw_rx_timestamp_dtu",
        "arrival_offset_dtu",
        "rx_error_dtu",
        "rx_error_ps",
        "fpi_samples",
        "fpi_equivalent_error_dtu",
        "fpi_equivalent_error_ps",
        "cir_match_error_dtu",
        "cir_match_error_ps",
        "cir_match_timestamp_dtu",
        "cir_match_score",
        "cir_match_peak_margin",
        "strongest_cir_path_ns",
        "cir_similarity",
        "rssi_dbm",
        "cia_fpi_offset_dtu",
    )
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for index in range(result.frame_count):
            raw_timestamp = int(result.raw_rx_timestamp_dtu[index])
            writer.writerow(
                {
                    "capture_id": int(result.capture_id[index]),
                    "sequence": int(result.sequence[index]),
                    "scheduled_time_dly": int(result.scheduled_time[index]),
                    "scheduled_timestamp_dtu": int(
                        result.scheduled_timestamp_dtu[index]
                    ),
                    "rx_timestamp_dtu": int(result.rx_timestamp_dtu[index]),
                    "raw_rx_timestamp_dtu": (
                        raw_timestamp if raw_timestamp >= 0 else ""
                    ),
                    "arrival_offset_dtu": int(result.arrival_offset_dtu[index]),
                    "rx_error_dtu": int(result.rx_error_dtu[index]),
                    "rx_error_ps": result.rx_error_ns[index] * 1e3,
                    "fpi_samples": result.first_path_index[index],
                    "fpi_equivalent_error_dtu": _optional_number(
                        result.fpi_equivalent_error_dtu[index]
                    ),
                    "fpi_equivalent_error_ps": _optional_number(
                        result.fpi_equivalent_error_ns[index] * 1e3
                    ),
                    "cir_match_error_dtu": _optional_number(
                        result.cir_match_error_dtu[index]
                    ),
                    "cir_match_error_ps": _optional_number(
                        result.cir_match_error_ns[index] * 1e3
                    ),
                    "cir_match_timestamp_dtu": _optional_number(
                        result.cir_match_timestamp_dtu[index]
                    ),
                    "cir_match_score": _optional_number(
                        result.cir_match_score[index]
                    ),
                    "cir_match_peak_margin": _optional_number(
                        result.cir_match_peak_margin[index]
                    ),
                    "strongest_cir_path_ns": result.strongest_path_ns[index],
                    "cir_similarity": _optional_number(
                        result.cir_similarity[index]
                    ),
                    "rssi_dbm": _optional_number(result.rssi_dbm[index]),
                    "cia_fpi_offset_dtu": _optional_number(
                        result.cia_fpi_offset_dtu[index]
                    ),
                }
            )


def _optional_number(value: float) -> float | str:
    return float(value) if np.isfinite(value) else ""
