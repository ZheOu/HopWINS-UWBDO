"""Interactive visualisation for HCIR v3 dual-CIR landmark timing."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pyqtgraph as pg
from PySide6 import QtCore, QtWidgets

from hopwins.analysis.spatial_landmarks import SpatialLandmarkResult

pg.setConfigOptions(antialias=False, background="#f4f5f6", foreground="#202428")


class SpatialTimingWindow(QtWidgets.QMainWindow):
    def __init__(
        self,
        result: SpatialLandmarkResult,
        source_path: str | Path,
        *,
        histogram_bin_dtu: int,
    ) -> None:
        super().__init__()
        self._result = result
        self.setWindowTitle(f"HopWINS Spatial CIR Timing - {Path(source_path).name}")
        self.resize(1420, 1320)
        self._build_ui(histogram_bin_dtu=max(histogram_bin_dtu, 1))

    def _build_ui(self, *, histogram_bin_dtu: int) -> None:
        central = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(central)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(8)
        layout.addLayout(self._build_summary())

        graphics = pg.GraphicsLayoutWidget()
        cir_plot = graphics.addPlot(row=0, col=0)
        cir_plot.setLabel("left", "CIR power", units="dB rel.")
        cir_plot.setLabel("bottom", "Time from landmark reference", units="ns")
        cir_plot.showGrid(x=True, y=True, alpha=0.2)
        cir_plot.addLegend(offset=(8, 8))
        self._add_cir_profile(cir_plot)
        x_lower, x_upper = _cir_x_range(self._result)
        y_lower, y_upper = _cir_y_range(self._result)
        cir_plot.setXRange(x_lower, x_upper, padding=0.0)
        cir_plot.setYRange(y_lower, y_upper, padding=0.0)
        cir_plot.addItem(
            pg.InfiniteLine(
                pos=0.0,
                angle=90,
                pen=pg.mkPen("#555b60", width=1.0),
            )
        )
        for index, landmark in enumerate(self._result.landmarks, start=1):
            position_ns = _dtu_to_ns(
                landmark.center_dtu - self._result.reference_landmark_dtu
            )
            line = pg.InfiniteLine(
                pos=position_ns,
                angle=90,
                pen=pg.mkPen("#7c4d99", width=1.0, style=QtCore.Qt.PenStyle.DashLine),
                label=f"L{index}",
                labelOpts={"position": 0.88, "color": "#7c4d99"},
            )
            cir_plot.addItem(line)

        histogram_plot = graphics.addPlot(row=1, col=0)
        histogram_plot.setLabel("left", "Frames")
        histogram_plot.setLabel("bottom", "Timing error from each median", units="ns")
        histogram_plot.showGrid(x=True, y=True, alpha=0.2)
        histogram_plot.addLegend(offset=(8, 8))
        half_range_ns = self._add_timestamp_histograms(
            histogram_plot,
            histogram_bin_dtu,
        )
        histogram_plot.setXRange(-half_range_ns, half_range_ns, padding=0.0)
        histogram_plot.addItem(
            pg.InfiniteLine(
                pos=0.0,
                angle=90,
                pen=pg.mkPen("#555b60", width=1.0),
            )
        )

        sequence_plot = graphics.addPlot(row=2, col=0)
        sequence_plot.setLabel("left", "Timing error", units="ns")
        sequence_plot.setLabel("bottom", "TX sequence")
        sequence_plot.showGrid(x=True, y=True, alpha=0.2)
        sequence_plot.addLegend(offset=(8, 8))
        self._add_sequence_timestamps(sequence_plot)

        confidence_plot = graphics.addPlot(row=3, col=0)
        confidence_plot.setLabel("left", "Landmark count")
        confidence_plot.setLabel("right", "Consensus quality")
        confidence_plot.setLabel("bottom", "TX sequence")
        confidence_plot.showGrid(x=True, y=True, alpha=0.2)
        confidence_plot.addLegend(offset=(8, 8))
        self._add_consensus_diagnostics(confidence_plot)

        residual_plot = graphics.addPlot(row=4, col=0)
        residual_plot.setLabel("left", "Landmark - consensus", units="ns")
        residual_plot.setLabel("bottom", "TX sequence")
        residual_plot.showGrid(x=True, y=True, alpha=0.2)
        residual_plot.addLegend(offset=(8, 8))
        self._add_landmark_residuals(residual_plot)
        residual_plot.addItem(
            pg.InfiniteLine(
                pos=0.0,
                angle=0,
                pen=pg.mkPen("#555b60", width=1.0),
            )
        )

        pdoa_plot = graphics.addPlot(row=5, col=0)
        pdoa_plot.setLabel("left", "CIA PDoA", units="rad")
        pdoa_plot.setLabel("bottom", "TX sequence")
        pdoa_plot.showGrid(x=True, y=True, alpha=0.2)
        pdoa_plot.addLegend(offset=(8, 8))
        valid_pdoa = np.isfinite(self._result.hardware_pdoa_radians)
        if np.any(valid_pdoa):
            pdoa_plot.plot(
                self._result.sequence[valid_pdoa],
                self._result.hardware_pdoa_radians[valid_pdoa],
                pen=pg.mkPen("#6a4c93", width=1.6),
                symbol="o",
                symbolSize=5,
                symbolBrush="#6a4c93",
                symbolPen=None,
                name="DW CIA diagnostic",
            )
        pdoa_plot.addItem(
            pg.InfiniteLine(
                pos=0.0,
                angle=0,
                pen=pg.mkPen("#555b60", width=1.0),
            )
        )

        graphics.ci.layout.setRowStretchFactor(0, 5)
        graphics.ci.layout.setRowStretchFactor(1, 3)
        graphics.ci.layout.setRowStretchFactor(2, 3)
        graphics.ci.layout.setRowStretchFactor(3, 2)
        graphics.ci.layout.setRowStretchFactor(4, 2)
        graphics.ci.layout.setRowStretchFactor(5, 2)
        layout.addWidget(graphics, stretch=1)
        self.setCentralWidget(central)
        self.statusBar().showMessage(
            f"HCIR v3 dual path RF{self._result.rf1_port}/RF{self._result.rf2_port}; "
            f"clock={self._result.clock_drift_mode} "
            f"({self._result.clock_frequency_error_ppb:.3f} ppb); "
            f"CIR view={x_lower:.2f} to {x_upper:.2f} ns; "
            f"histogram=+/-{half_range_ns:.3f} ns"
        )

    def _add_cir_profile(self, plot: pg.PlotItem) -> None:
        result = self._result
        rf1_low = pg.PlotCurveItem(
            result.cir_time_ns,
            result.rf1_p10_power_db,
            pen=pg.mkPen("#9bb7cc", width=0.8),
        )
        rf1_high = pg.PlotCurveItem(
            result.cir_time_ns,
            result.rf1_p90_power_db,
            pen=pg.mkPen("#9bb7cc", width=0.8),
        )
        plot.addItem(rf1_low)
        plot.addItem(rf1_high)
        plot.addItem(
            pg.FillBetweenItem(
                rf1_low,
                rf1_high,
                brush=pg.mkBrush(31, 119, 180, 34),
            )
        )
        rf2_low = pg.PlotCurveItem(
            result.cir_time_ns,
            result.rf2_p10_power_db,
            pen=pg.mkPen("#d9aa8c", width=0.8),
        )
        rf2_high = pg.PlotCurveItem(
            result.cir_time_ns,
            result.rf2_p90_power_db,
            pen=pg.mkPen("#d9aa8c", width=0.8),
        )
        plot.addItem(rf2_low)
        plot.addItem(rf2_high)
        plot.addItem(
            pg.FillBetweenItem(
                rf2_low,
                rf2_high,
                brush=pg.mkBrush(217, 115, 13, 34),
            )
        )
        plot.plot(
            result.cir_time_ns,
            result.rf1_mean_power_db,
            pen=pg.mkPen("#1769aa", width=2.0),
            name=f"RF{result.rf1_port} mean",
        )
        plot.plot(
            result.cir_time_ns,
            result.rf2_mean_power_db,
            pen=pg.mkPen("#c15d00", width=2.0),
            name=f"RF{result.rf2_port} mean",
        )

    def _add_timestamp_histograms(
        self,
        plot: pg.PlotItem,
        bin_dtu: int,
    ) -> float:
        values = (
            ("DW RXTS", self._result.rx_error_dtu, "#8f3260"),
            (
                "Full CIR correlation",
                self._result.cir_correlation_error_dtu,
                "#15809a",
            ),
            ("Spatial landmarks", self._result.landmark_error_dtu, "#21865b"),
        )
        edges_dtu = _dynamic_histogram_edges(
            np.concatenate(
                [array[np.isfinite(array)] for _, array, _ in values]
            ),
            bin_dtu,
        )
        edges_ns = _dtu_to_ns(edges_dtu)
        for name, data, color in values:
            valid = data[np.isfinite(data)]
            if not valid.size:
                continue
            counts, _ = np.histogram(valid, bins=edges_dtu)
            plot.plot(
                edges_ns,
                counts,
                stepMode="center",
                pen=pg.mkPen(color, width=1.9),
                name=name,
            )
        return max(abs(float(edges_ns[0])), abs(float(edges_ns[-1])))

    def _add_sequence_timestamps(self, plot: pg.PlotItem) -> None:
        values = (
            ("DW RXTS", self._result.rx_error_ns, "o", "#8f3260"),
            (
                "Full CIR correlation",
                self._result.cir_correlation_error_ns,
                "x",
                "#15809a",
            ),
            ("Spatial landmarks", self._result.landmark_error_ns, "d", "#21865b"),
        )
        for name, error, symbol, color in values:
            valid = np.isfinite(error)
            if not np.any(valid):
                continue
            plot.plot(
                self._result.sequence[valid],
                error[valid],
                pen=None,
                symbol=symbol,
                symbolSize=7,
                symbolBrush=color,
                symbolPen=pg.mkPen(color, width=1.1),
                name=name,
            )

    def _add_landmark_residuals(self, plot: pg.PlotItem) -> None:
        result = self._result
        colors = (
            "#1769aa",
            "#c15d00",
            "#2e7d32",
            "#7c4d99",
            "#8f3260",
            "#15809a",
            "#795548",
            "#455a64",
        )
        for landmark_index, _ in enumerate(result.landmarks):
            shifts = result.landmark_shift_dtu[:, landmark_index]
            valid = np.isfinite(shifts) & np.isfinite(result.landmark_vote_dtu)
            if not np.any(valid):
                continue
            residual_ns = _dtu_to_ns(shifts - result.landmark_vote_dtu)
            color = colors[landmark_index % len(colors)]
            inliers = valid & result.landmark_inlier_mask[:, landmark_index]
            outliers = valid & ~result.landmark_inlier_mask[:, landmark_index]
            if np.any(inliers):
                plot.plot(
                    result.sequence[inliers],
                    residual_ns[inliers],
                    pen=None,
                    symbol="o",
                    symbolSize=6,
                    symbolBrush=color,
                    symbolPen=None,
                    name=f"L{landmark_index + 1} inlier",
                )
            if np.any(outliers):
                plot.plot(
                    result.sequence[outliers],
                    residual_ns[outliers],
                    pen=None,
                    symbol="x",
                    symbolSize=7,
                    symbolBrush=None,
                    symbolPen=pg.mkPen(color, width=1.2),
                    name=f"L{landmark_index + 1} outlier",
                )

    def _add_consensus_diagnostics(self, plot: pg.PlotItem) -> None:
        result = self._result
        plot.plot(
            result.sequence,
            result.landmark_candidate_count,
            pen=pg.mkPen("#607d8b", width=1.5),
            symbol="o",
            symbolSize=5,
            symbolBrush="#607d8b",
            symbolPen=None,
            name="Candidates",
        )
        plot.plot(
            result.sequence,
            result.landmark_inlier_count,
            pen=pg.mkPen("#2e7d32", width=1.8),
            symbol="t",
            symbolSize=7,
            symbolBrush="#2e7d32",
            symbolPen=None,
            name="Consensus inliers",
        )
        quality_view = pg.ViewBox()
        plot.scene().addItem(quality_view)
        plot.showAxis("right")
        plot.getAxis("right").linkToView(quality_view)
        quality_view.setXLink(plot)
        quality_view.setYRange(0.0, 1.0, padding=0.05)

        def sync_quality_view() -> None:
            quality_view.setGeometry(plot.getViewBox().sceneBoundingRect())
            quality_view.linkedViewChanged(plot.getViewBox(), quality_view.XAxis)

        sync_quality_view()
        plot.getViewBox().sigResized.connect(sync_quality_view)
        valid_quality = np.isfinite(result.landmark_quality)
        if np.any(valid_quality):
            quality_curve = pg.PlotDataItem(
                result.sequence[valid_quality],
                result.landmark_quality[valid_quality],
                pen=None,
                symbol="d",
                symbolSize=6,
                symbolBrush="#7c4d99",
                symbolPen=None,
            )
            quality_view.addItem(quality_curve)
            plot.legend.addItem(quality_curve, "Consensus quality")
        self._quality_view = quality_view

    def _build_summary(self) -> QtWidgets.QGridLayout:
        fields = (
            (
                "Pairs",
                f"{self._result.frame_count} / "
                f"{self._result.source_capture_count} captures",
            ),
            ("RF paths", f"RF{self._result.rf1_port} and RF{self._result.rf2_port}"),
            ("Landmarks", str(len(self._result.landmarks))),
            ("Sequence gaps", str(self._result.missing_sequence_count)),
            ("DW RXTS sigma", _sigma_text(self._result.rx_error_ns)),
            (
                "CIR corr sigma",
                _sigma_text(self._result.cir_correlation_error_ns),
            ),
            ("Spatial sigma", _sigma_text(self._result.landmark_error_ns)),
            ("Spatial coverage", _coverage_text(self._result.landmark_error_ns)),
            (
                "Clock error",
                f"{self._result.clock_frequency_error_ppb:.3f} ppb",
            ),
            (
                "Track reliability",
                _reliability_text(self._result.landmark_track_reliability),
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


def run_spatial_timing_window(
    result: SpatialLandmarkResult,
    source_path: str | Path,
    *,
    histogram_bin_dtu: int,
) -> int:
    application = QtWidgets.QApplication.instance()
    owns_application = application is None
    if application is None:
        application = QtWidgets.QApplication(sys.argv)
    window = SpatialTimingWindow(
        result,
        source_path,
        histogram_bin_dtu=histogram_bin_dtu,
    )
    window.show()
    if owns_application:
        return application.exec()
    return 0


def _cir_x_range(result: SpatialLandmarkResult) -> tuple[float, float]:
    combined = np.maximum(result.rf1_mean_power_db, result.rf2_mean_power_db)
    peak = float(np.max(combined, initial=-90.0))
    active = combined >= peak - 35.0
    if not np.any(active):
        return float(result.cir_time_ns[0]), float(result.cir_time_ns[-1])
    lower = float(np.min(result.cir_time_ns[active]))
    upper = float(np.max(result.cir_time_ns[active]))
    margin = max(1.0, 0.15 * max(upper - lower, 1.0))
    return lower - margin, upper + margin


def _cir_y_range(result: SpatialLandmarkResult) -> tuple[float, float]:
    values = np.concatenate((result.rf1_p10_power_db, result.rf2_p10_power_db))
    lower = min(float(np.min(values)) - 3.0, -20.0)
    upper = max(
        float(np.max(result.rf1_mean_power_db)),
        float(np.max(result.rf2_mean_power_db)),
    ) + 3.0
    return max(lower, -100.0), upper


def _dynamic_histogram_edges(values: np.ndarray, bin_dtu: int) -> np.ndarray:
    if not values.size:
        return np.asarray([-bin_dtu, 0, bin_dtu], dtype=np.float64)
    half_range = max(
        bin_dtu * 4,
        int(np.ceil(float(np.max(np.abs(values))) * 1.12 / bin_dtu)) * bin_dtu,
    )
    return np.arange(-half_range, half_range + bin_dtu, bin_dtu, dtype=np.float64)


def _sigma_text(values: np.ndarray) -> str:
    valid = values[np.isfinite(values)]
    return f"{np.std(valid) * 1e3:.2f} ps" if valid.size else "-"


def _coverage_text(values: np.ndarray) -> str:
    return f"{np.count_nonzero(np.isfinite(values))} / {values.size}"


def _reliability_text(values: np.ndarray) -> str:
    if not values.size:
        return "-"
    return f"median {np.median(values):.3f}, min {np.min(values):.3f}"


def _dtu_to_ns(values: np.ndarray | float) -> np.ndarray | float:
    return values * (1.0 / (499.2e6 * 128.0)) * 1e9
