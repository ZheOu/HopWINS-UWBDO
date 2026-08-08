"""Interactive plots for the shared-clock static timing experiment."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pyqtgraph as pg
from PySide6 import QtCore, QtWidgets

from hopwins.analysis.static_timing import StaticTimingResult

pg.setConfigOptions(antialias=False, background="#f4f5f6", foreground="#202428")


class StaticTimingWindow(QtWidgets.QMainWindow):
    def __init__(
        self,
        result: StaticTimingResult,
        source_path: str | Path,
        *,
        histogram_bin_dtu: int,
        histogram_min_half_range_ns: float,
        view_pre_ns: float,
        view_post_ns: float,
    ) -> None:
        super().__init__()
        self._result = result
        self.setWindowTitle(f"HopWINS Static Timing - {Path(source_path).name}")
        self.resize(1380, 1060)
        self._build_ui(
            histogram_bin_dtu=max(histogram_bin_dtu, 1),
            histogram_min_half_range_ns=histogram_min_half_range_ns,
            view_pre_ns=view_pre_ns,
            view_post_ns=view_post_ns,
        )

    def _build_ui(
        self,
        *,
        histogram_bin_dtu: int,
        histogram_min_half_range_ns: float,
        view_pre_ns: float,
        view_post_ns: float,
    ) -> None:
        central = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(central)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(8)
        layout.addLayout(self._build_summary())

        graphics = pg.GraphicsLayoutWidget()
        cir_plot = graphics.addPlot(row=0, col=0)
        cir_plot.setLabel("left", "CIR power", units="dB rel.")
        cir_plot.setLabel("bottom", "Time from median RX arrival", units="ns")
        cir_plot.setYRange(-60.0, 3.0)
        cir_plot.showGrid(x=True, y=True, alpha=0.2)
        cir_plot.addLegend(offset=(8, 8))

        lower_curve = pg.PlotCurveItem(
            self._result.common_time_ns,
            self._result.power_p10_db,
            pen=pg.mkPen("#8d969d", width=0.8),
        )
        upper_curve = pg.PlotCurveItem(
            self._result.common_time_ns,
            self._result.power_p90_db,
            pen=pg.mkPen("#8d969d", width=0.8),
        )
        cir_plot.addItem(lower_curve)
        cir_plot.addItem(upper_curve)
        cir_plot.addItem(
            pg.FillBetweenItem(
                lower_curve,
                upper_curve,
                brush=pg.mkBrush(114, 126, 136, 45),
            )
        )
        cir_plot.plot(
            self._result.common_time_ns,
            self._result.mean_power_db,
            pen=pg.mkPen("#13795b", width=2.2),
            name="Mean power",
        )
        cir_plot.plot(
            self._result.common_time_ns,
            self._result.median_power_db,
            pen=pg.mkPen("#1769aa", width=1.5),
            name="Median power",
        )
        cir_plot.plot(
            self._result.common_time_ns,
            self._result.coherent_power_db,
            pen=pg.mkPen("#d17818", width=1.3, style=QtCore.Qt.PenStyle.DashLine),
            name="Phase-aligned coherent",
        )
        cir_plot.plot(
            self._result.common_time_ns,
            self._result.first_frame_power_db,
            pen=pg.mkPen("#747b80", width=1.0),
            name="First frame",
        )

        histogram_plot = graphics.addPlot(row=1, col=0)
        histogram_plot.setLabel("left", "Frames")
        histogram_plot.setLabel(
            "bottom",
            "RX timing error from median",
            units="ns",
        )
        histogram_plot.showGrid(x=True, y=True, alpha=0.2)
        histogram_legend = histogram_plot.addLegend(offset=(8, 8))
        valid_fpi = np.isfinite(self._result.fpi_equivalent_error_dtu)
        valid_match = np.isfinite(self._result.cir_match_error_dtu)
        histogram_values_dtu = self._result.rx_error_dtu.astype(np.float64)
        if np.any(valid_fpi):
            histogram_values_dtu = np.concatenate(
                (
                    histogram_values_dtu,
                    self._result.fpi_equivalent_error_dtu[valid_fpi],
                )
            )
        if np.any(valid_match):
            histogram_values_dtu = np.concatenate(
                (
                    histogram_values_dtu,
                    self._result.cir_match_error_dtu[valid_match],
                )
            )
        edges_dtu, _ = _histogram(
            histogram_values_dtu,
            histogram_bin_dtu,
        )
        _, rx_counts = _histogram(
            self._result.rx_error_dtu.astype(np.float64),
            histogram_bin_dtu,
            fixed_edges=edges_dtu,
        )
        edges_ns = _dtu_edges_to_ns(edges_dtu)
        rx_bars = pg.BarGraphItem(
            x=(edges_ns[:-1] + edges_ns[1:]) * 0.5,
            height=rx_counts,
            width=np.diff(edges_ns) * 0.9,
            brush=pg.mkBrush("#8f3260"),
            pen=pg.mkPen("#6e2046"),
        )
        histogram_plot.addItem(rx_bars)
        histogram_legend.addItem(rx_bars, "RXTS")

        if np.any(valid_fpi):
            fpi_edges_dtu, fpi_counts = _histogram(
                self._result.fpi_equivalent_error_dtu[valid_fpi],
                histogram_bin_dtu,
                fixed_edges=edges_dtu,
            )
            histogram_plot.plot(
                _dtu_edges_to_ns(fpi_edges_dtu),
                fpi_counts,
                stepMode=True,
                fillLevel=None,
                pen=pg.mkPen("#15809a", width=1.8),
                name="RAWST + FPI",
            )
        histogram_half_range_ns = _histogram_half_range_ns(
            _dtu_edges_to_ns(histogram_values_dtu),
            histogram_min_half_range_ns,
        )
        histogram_plot.setXRange(
            -histogram_half_range_ns,
            histogram_half_range_ns,
            padding=0.0,
        )
        histogram_plot.addItem(
            pg.InfiniteLine(
                pos=0.0,
                angle=90,
                pen=pg.mkPen("#555b60", width=1.0),
            )
        )

        matched_histogram_plot = graphics.addPlot(row=2, col=0)
        matched_histogram_plot.setLabel("left", "Frames")
        matched_histogram_plot.setLabel(
            "bottom",
            "CIR-match timing error from median",
            units="ns",
        )
        matched_histogram_plot.showGrid(x=True, y=True, alpha=0.2)
        matched_histogram_plot.setXLink(histogram_plot)
        matched_legend = matched_histogram_plot.addLegend(offset=(8, 8))
        if np.any(valid_match):
            _, matched_counts = _histogram(
                self._result.cir_match_error_dtu[valid_match],
                histogram_bin_dtu,
                fixed_edges=edges_dtu,
            )
            matched_bars = pg.BarGraphItem(
                x=(edges_ns[:-1] + edges_ns[1:]) * 0.5,
                height=matched_counts,
                width=np.diff(edges_ns) * 0.9,
                brush=pg.mkBrush("#21865b"),
                pen=pg.mkPen("#166241"),
            )
            matched_histogram_plot.addItem(matched_bars)
            matched_legend.addItem(
                matched_bars,
                f"CIR match ({self._result.cir_match_mode})",
            )
        matched_histogram_plot.addItem(
            pg.InfiniteLine(
                pos=0.0,
                angle=90,
                pen=pg.mkPen("#555b60", width=1.0),
            )
        )

        sequence_plot = graphics.addPlot(row=3, col=0)
        sequence_plot.setLabel("left", "Timing", units="ns")
        sequence_plot.setLabel("bottom", "TX sequence")
        sequence_plot.showGrid(x=True, y=True, alpha=0.2)
        sequence_plot.addLegend(offset=(8, 8))
        sequence_plot.plot(
            self._result.sequence,
            self._result.rx_error_ns,
            pen=None,
            symbol="o",
            symbolSize=6,
            symbolBrush="#8f3260",
            symbolPen=None,
            name="RXTS",
        )
        if np.any(valid_fpi):
            sequence_plot.plot(
                self._result.sequence[valid_fpi],
                self._result.fpi_equivalent_error_ns[valid_fpi],
                pen=None,
                symbol="x",
                symbolSize=8,
                symbolBrush="#15809a",
                symbolPen=pg.mkPen("#15809a", width=1.3),
                name="RAWST + FPI",
            )
        if np.any(valid_match):
            sequence_plot.plot(
                self._result.sequence[valid_match],
                self._result.cir_match_error_ns[valid_match],
                pen=None,
                symbol="d",
                symbolSize=7,
                symbolBrush="#21865b",
                symbolPen=None,
                name=f"CIR match ({self._result.cir_match_mode})",
            )
        sequence_plot.plot(
            self._result.sequence,
            self._result.strongest_path_ns,
            pen=None,
            symbol="t",
            symbolSize=7,
            symbolBrush="#d17818",
            symbolPen=None,
            name="Strongest CIR tap",
        )

        graphics.ci.layout.setRowStretchFactor(0, 5)
        graphics.ci.layout.setRowStretchFactor(1, 2)
        graphics.ci.layout.setRowStretchFactor(2, 2)
        graphics.ci.layout.setRowStretchFactor(3, 3)
        layout.addWidget(graphics, stretch=1)
        self.setCentralWidget(central)

        if view_pre_ns > 0.0 and view_post_ns > 0.0:
            cir_plot.setXRange(-view_pre_ns, view_post_ns, padding=0.0)
        cir_plot.addItem(
            pg.InfiniteLine(
                pos=0.0,
                angle=90,
                pen=pg.mkPen("#555b60", width=1.0),
            )
        )
        self.statusBar().showMessage(
            f"RXTS and CIR share the TX scheduled-time reference; "
            f"origin=median arrival {self._result.arrival_origin_dtu} DTU; "
            f"histogram=+/-{histogram_half_range_ns:.0f} ns"
        )

    def _build_summary(self) -> QtWidgets.QGridLayout:
        result = self._result
        rx_std_ps = float(np.std(result.rx_error_ns)) * 1e3
        rx_peak_to_peak_ps = float(np.ptp(result.rx_error_ns)) * 1e3
        similarity = result.cir_similarity[np.isfinite(result.cir_similarity)]
        rssi = result.rssi_dbm[np.isfinite(result.rssi_dbm)]
        cia_offset = result.cia_fpi_offset_dtu[
            np.isfinite(result.cia_fpi_offset_dtu)
        ]
        match_valid = np.isfinite(result.cir_match_error_ns)
        matched = result.cir_match_error_ns[match_valid]
        match_scores = result.cir_match_score[
            np.isfinite(result.cir_match_score)
        ]
        match_margins = result.cir_match_peak_margin[
            np.isfinite(result.cir_match_peak_margin)
        ]
        matched_std_ps = float(np.std(matched)) * 1e3 if matched.size else np.nan
        improvement = (
            rx_std_ps / matched_std_ps
            if np.isfinite(matched_std_ps) and matched_std_ps > 0.0
            else np.nan
        )
        fields = (
            ("Frames", f"{result.frame_count} used / {result.source_capture_count}"),
            (
                "RF / sequence gaps",
                f"RF{result.rf_port} / {result.missing_sequence_count}",
            ),
            ("RXTS sigma", f"{rx_std_ps:.2f} ps"),
            ("RXTS peak-to-peak", f"{rx_peak_to_peak_ps:.2f} ps"),
            (
                "TX interval",
                (
                    f"{result.transmit_interval_us:.6f} us"
                    if result.transmit_interval_us is not None
                    else "-"
                ),
            ),
            (
                "CIR similarity",
                (
                    f"median {np.median(similarity):.5f}, "
                    f"min {np.min(similarity):.5f}"
                    if similarity.size
                    else "-"
                ),
            ),
            (
                "RSSI",
                (
                    f"{np.mean(rssi):.2f} +/- {np.std(rssi):.2f} dBm"
                    if rssi.size
                    else "-"
                ),
            ),
            (
                "CIA-FPI offset",
                (
                    f"{np.median(cia_offset):.1f} DTU, "
                    f"span {np.ptp(cia_offset):.1f}"
                    if cia_offset.size
                    else "-"
                ),
            ),
            (
                "CIR-match sigma",
                f"{matched_std_ps:.2f} ps" if np.isfinite(matched_std_ps) else "-",
            ),
            (
                "CIR-match peak-to-peak",
                (
                    f"{np.ptp(matched) * 1e3:.2f} ps"
                    if matched.size
                    else "-"
                ),
            ),
            (
                "Match score",
                (
                    f"median {np.median(match_scores):.5f}, "
                    f"min {np.min(match_scores):.5f}"
                    if match_scores.size
                    else "-"
                ),
            ),
            (
                "Margin / improvement",
                (
                    f"{np.median(match_margins):.5f} / {improvement:.2f}x"
                    if match_margins.size and np.isfinite(improvement)
                    else "-"
                ),
            ),
        )

        summary = QtWidgets.QGridLayout()
        summary.setHorizontalSpacing(18)
        summary.setVerticalSpacing(5)
        for index, (title, value) in enumerate(fields):
            row = index // 4
            column = (index % 4) * 2
            title_label = QtWidgets.QLabel(title)
            title_label.setStyleSheet("color: #5f666d; font-weight: 600;")
            value_label = QtWidgets.QLabel(value)
            value_label.setTextInteractionFlags(
                QtCore.Qt.TextInteractionFlag.TextSelectableByMouse
            )
            summary.addWidget(title_label, row, column)
            summary.addWidget(value_label, row, column + 1)
        return summary


def run_static_timing_window(
    result: StaticTimingResult,
    source_path: str | Path,
    *,
    histogram_bin_dtu: int,
    histogram_min_half_range_ns: float,
    view_pre_ns: float,
    view_post_ns: float,
) -> int:
    application = QtWidgets.QApplication.instance()
    owns_application = application is None
    if application is None:
        application = QtWidgets.QApplication(sys.argv)
    window = StaticTimingWindow(
        result,
        source_path,
        histogram_bin_dtu=histogram_bin_dtu,
        histogram_min_half_range_ns=histogram_min_half_range_ns,
        view_pre_ns=view_pre_ns,
        view_post_ns=view_post_ns,
    )
    window.show()
    if owns_application:
        return application.exec()
    return 0


def _histogram(
    values: np.ndarray,
    bin_width: int,
    *,
    fixed_edges: np.ndarray | None = None,
) -> tuple[np.ndarray, np.ndarray]:
    if fixed_edges is not None:
        counts, _ = np.histogram(values, bins=fixed_edges)
        return fixed_edges, counts

    minimum = float(np.min(values))
    maximum = float(np.max(values))
    start = np.floor(minimum / bin_width) * bin_width - bin_width * 0.5
    stop = np.ceil(maximum / bin_width) * bin_width + bin_width * 0.5
    if stop <= start:
        stop = start + bin_width
    edges = np.arange(start, stop + bin_width, bin_width, dtype=np.float64)
    counts, _ = np.histogram(values, bins=edges)
    return edges, counts


def _dtu_edges_to_ns(values: np.ndarray) -> np.ndarray:
    from hopwins.analysis.statistics import DW_TIME_UNIT_SECONDS

    return values * DW_TIME_UNIT_SECONDS * 1e9


def _histogram_half_range_ns(
    values_ns: np.ndarray,
    minimum_half_range_ns: float,
) -> float:
    if minimum_half_range_ns <= 0.0:
        raise ValueError("histogram minimum half range must be positive")
    finite = values_ns[np.isfinite(values_ns)]
    if finite.size == 0:
        return minimum_half_range_ns
    maximum = float(np.max(np.abs(finite), initial=0.0))
    margin = max(1.0, maximum * 0.1)
    return max(minimum_half_range_ns, float(np.ceil(maximum + margin)))
