"""Dynamic HCIR v3 STS0/STS1 dual-channel CIR visualization."""

from __future__ import annotations

import math
import queue
import sys

import numpy as np
import pyqtgraph as pg
from PySide6 import QtCore, QtGui, QtWidgets

from hopwins.analysis.cir import decode_i24_q24
from hopwins.capture.pairing import DualCirCapture, DualCirPairAssembler
from hopwins.io.workers import (
    CaptureEvent,
    ProfileEvent,
    ReplayWorker,
    SerialWorker,
    TextEvent,
    WorkerError,
    WorkerEvent,
)

DW_TIME_UNIT_NS = 1.0 / (499.2e6 * 128.0) * 1e9

pg.setConfigOptions(antialias=False, background="#f4f5f6", foreground="#202428")


class DualCirMonitorWindow(QtWidgets.QMainWindow):
    def __init__(
        self,
        worker: SerialWorker | ReplayWorker,
        events: queue.Queue[WorkerEvent],
        source_name: str,
        refresh_hz: int = 30,
    ) -> None:
        super().__init__()
        self._worker = worker
        self._events = events
        self._pairer = DualCirPairAssembler()
        self._labels: dict[str, QtWidgets.QLabel] = {}

        self.setWindowTitle(f"HopWINS STS Dual CIR Monitor - {source_name}")
        self.resize(1440, 960)
        self._build_ui()

        self._timer = QtCore.QTimer(self)
        self._timer.setInterval(max(10, round(1000 / refresh_hz)))
        self._timer.timeout.connect(self._drain_events)
        self._timer.start()
        self._worker.start()

    def _build_ui(self) -> None:
        central = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(central)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(8)

        summary = QtWidgets.QGridLayout()
        summary.setHorizontalSpacing(18)
        summary.setVerticalSpacing(5)
        fields = (
            ("profile", "Firmware"),
            ("capture", "Capture pair"),
            ("channels", "Channels"),
            ("samples", "CIR windows"),
            ("first_path", "First paths"),
            ("rssi", "RSSI"),
            ("toa", "STS ToA"),
            ("pdoa", "PDoA / TDoA"),
            ("status", "Channel status"),
            ("integrity", "Stream integrity"),
        )
        for index, (key, title) in enumerate(fields):
            row = index // 2
            column = (index % 2) * 2
            title_label = QtWidgets.QLabel(title)
            title_label.setStyleSheet("color: #5f666d; font-weight: 600;")
            value_label = QtWidgets.QLabel("-")
            value_label.setTextInteractionFlags(
                QtCore.Qt.TextInteractionFlag.TextSelectableByMouse
            )
            value_label.setWordWrap(True)
            summary.addWidget(title_label, row, column)
            summary.addWidget(value_label, row, column + 1)
            self._labels[key] = value_label
        layout.addLayout(summary)

        splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Vertical)
        self._magnitude_plot = pg.PlotWidget()
        self._magnitude_plot.setLabel("left", "Magnitude", units="dB common ref.")
        self._magnitude_plot.setLabel("bottom", "Sample offset from channel FPI")
        self._magnitude_plot.setYRange(-80.0, 2.0)
        self._magnitude_plot.showGrid(x=True, y=True, alpha=0.2)
        self._sts0_magnitude = self._magnitude_plot.plot(
            pen=pg.mkPen("#1261a0", width=1.8), name="STS0 / RF1"
        )
        self._sts1_magnitude = self._magnitude_plot.plot(
            pen=pg.mkPen("#c04b36", width=1.8), name="STS1 / RF2"
        )
        self._magnitude_plot.addLegend(offset=(8, 8))
        self._magnitude_plot.addItem(
            pg.InfiniteLine(angle=90, pos=0.0, pen=pg.mkPen("#6d4c8f", width=1.2))
        )

        raw_container = QtWidgets.QWidget()
        raw_layout = QtWidgets.QHBoxLayout(raw_container)
        raw_layout.setContentsMargins(0, 0, 0, 0)
        self._sts0_plot, self._sts0_i, self._sts0_q = _raw_plot(
            "STS0 / RF1 raw I/Q"
        )
        self._sts1_plot, self._sts1_i, self._sts1_q = _raw_plot(
            "STS1 / RF2 raw I/Q"
        )
        raw_layout.addWidget(self._sts0_plot)
        raw_layout.addWidget(self._sts1_plot)

        splitter.addWidget(self._magnitude_plot)
        splitter.addWidget(raw_container)
        splitter.setSizes([430, 360])
        layout.addWidget(splitter, stretch=1)

        self._console = QtWidgets.QPlainTextEdit()
        self._console.setReadOnly(True)
        self._console.setMaximumBlockCount(150)
        self._console.setMaximumHeight(110)
        self._console.setStyleSheet(
            "font-family: Menlo, Consolas, monospace; font-size: 11px;"
        )
        layout.addWidget(self._console)

        self.setCentralWidget(central)
        self.statusBar().showMessage("Waiting for an HCIR v3 STS0/STS1 pair")

    @QtCore.Slot()
    def _drain_events(self) -> None:
        latest_pair: DualCirCapture | None = None
        for _ in range(1024):
            try:
                event = self._events.get_nowait()
            except queue.Empty:
                break
            if isinstance(event, CaptureEvent):
                pair = self._pairer.add(event.capture)
                if pair is not None:
                    latest_pair = pair
            elif isinstance(event, ProfileEvent):
                self._labels["profile"].setText(
                    f"{event.profile.board} / {event.profile.role}"
                )
            elif isinstance(event, TextEvent):
                self._console.appendPlainText(event.line)
            elif isinstance(event, WorkerError):
                self.statusBar().showMessage(event.message)
                self._console.appendPlainText(f"ERROR: {event.message}")

        if latest_pair is not None:
            self._update_pair(latest_pair)

    def _update_pair(self, pair: DualCirCapture) -> None:
        sts0 = pair.sts0
        sts1 = pair.sts1
        header0 = sts0.header
        header1 = sts1.header
        self._labels["capture"].setText(str(pair.capture_id))
        self._labels["channels"].setText(
            f"STS0/RF{header0.cir_source_rf_port or header0.rf_port or '?'} + "
            f"STS1/RF{header1.cir_source_rf_port or header1.rf_port or '?'}"
        )
        self._labels["samples"].setText(
            f"STS0 [{header0.capture_sample_offset}, "
            f"{header0.capture_sample_offset + header0.capture_sample_count}) / "
            f"STS1 [{header1.capture_sample_offset}, "
            f"{header1.capture_sample_offset + header1.capture_sample_count})"
        )
        self._labels["first_path"].setText(
            f"{header0.first_path_index:.3f} / {header1.first_path_index:.3f}"
        )
        self._labels["rssi"].setText(
            f"{_format_dbm(header0.rssi_dbm)} / {_format_dbm(header1.rssi_dbm)}"
        )
        self._labels["toa"].setText(
            f"STS0=0x{header0.sts0_timestamp:010X} "
            f"STS1=0x{header0.sts1_timestamp:010X}"
        )
        pdoa = header0.pdoa_radians
        pdoa_text = "unavailable"
        if pdoa is not None:
            pdoa_text = f"{pdoa:.6f} rad ({math.degrees(pdoa):.3f} deg)"
        self._labels["pdoa"].setText(
            f"{pdoa_text}; TDoA={header0.tdoa_dtu} DTU "
            f"({header0.tdoa_dtu * DW_TIME_UNIT_NS:.6f} ns)"
        )
        self._labels["status"].setText(
            f"STS0 diag/cir={header0.diagnostic_status}/{header0.cir_status}; "
            f"STS1 diag/cir={header1.diagnostic_status}/{header1.cir_status}; "
            f"PDoA={header0.pdoa_diagnostic_status}"
        )
        parser = self._worker.parser.statistics
        assembler = self._worker.assembler.statistics
        pairing = self._pairer.statistics
        self._labels["integrity"].setText(
            f"CRC={parser.crc_errors} frame={parser.framing_errors} "
            f"capture_incomplete={assembler.incomplete_captures} "
            f"pair_incomplete={pairing.incomplete_pairs}"
        )

        if not sts0.has_cir or not sts1.has_cir:
            self._clear_curves()
            self.statusBar().showMessage(f"Capture {pair.capture_id}: CIR unavailable")
            return

        i0, q0 = decode_i24_q24(sts0.cir_bytes)
        i1, q1 = decode_i24_q24(sts1.cir_bytes)
        x0 = (
            np.arange(len(i0), dtype=np.float64)
            + header0.capture_sample_offset
            - header0.first_path_index
        )
        x1 = (
            np.arange(len(i1), dtype=np.float64)
            + header1.capture_sample_offset
            - header1.first_path_index
        )
        db0, db1 = _common_reference_db(i0, q0, i1, q1)
        self._sts0_magnitude.setData(x0, db0)
        self._sts1_magnitude.setData(x1, db1)
        self._sts0_i.setData(x0, i0)
        self._sts0_q.setData(x0, q0)
        self._sts1_i.setData(x1, i1)
        self._sts1_q.setData(x1, q1)
        self.statusBar().showMessage(
            f"Capture {pair.capture_id}: complete STS0/STS1 pair"
        )

    def _clear_curves(self) -> None:
        for curve in (
            self._sts0_magnitude,
            self._sts1_magnitude,
            self._sts0_i,
            self._sts0_q,
            self._sts1_i,
            self._sts1_q,
        ):
            curve.setData([], [])

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self._timer.stop()
        self._worker.stop()
        event.accept()


def run_dual_monitor(
    worker: SerialWorker | ReplayWorker,
    events: queue.Queue[WorkerEvent],
    source_name: str,
    refresh_hz: int = 30,
) -> int:
    application = QtWidgets.QApplication.instance()
    owns_application = application is None
    if application is None:
        application = QtWidgets.QApplication(sys.argv)
    window = DualCirMonitorWindow(
        worker,
        events,
        source_name,
        refresh_hz,
    )
    window.show()
    try:
        if owns_application:
            return application.exec()
        return 0
    finally:
        if owns_application:
            worker.stop()


def _raw_plot(
    title: str,
) -> tuple[pg.PlotWidget, pg.PlotDataItem, pg.PlotDataItem]:
    plot = pg.PlotWidget(title=title)
    plot.setLabel("left", "Accumulator")
    plot.setLabel("bottom", "Sample offset from channel FPI")
    plot.showGrid(x=True, y=True, alpha=0.2)
    i_curve = plot.plot(pen=pg.mkPen("#1261a0", width=1.0), name="I")
    q_curve = plot.plot(pen=pg.mkPen("#c04b36", width=1.0), name="Q")
    plot.addLegend(offset=(8, 8))
    plot.addItem(
        pg.InfiniteLine(angle=90, pos=0.0, pen=pg.mkPen("#6d4c8f", width=1.0))
    )
    return plot, i_curve, q_curve


def _common_reference_db(
    i0: np.ndarray,
    q0: np.ndarray,
    i1: np.ndarray,
    q1: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    magnitude0 = np.hypot(i0.astype(np.float64), q0.astype(np.float64))
    magnitude1 = np.hypot(i1.astype(np.float64), q1.astype(np.float64))
    reference = max(
        float(magnitude0.max(initial=0.0)),
        float(magnitude1.max(initial=0.0)),
        1.0,
    )
    return (
        20.0 * np.log10(np.maximum(magnitude0, 1.0) / reference),
        20.0 * np.log10(np.maximum(magnitude1, 1.0) / reference),
    )


def _format_dbm(value: float | None) -> str:
    return "-" if value is None else f"{value:.2f} dBm"
