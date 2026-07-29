"""PySide6/PyQtGraph live CIR monitor."""

from __future__ import annotations

import queue
import sys
from collections.abc import Mapping
from pathlib import Path

import numpy as np
import pyqtgraph as pg
from PySide6 import QtCore, QtGui, QtWidgets

from hopwins.analysis.cir import decode_i24_q24, magnitude_db
from hopwins.analysis.statistics import CaptureStatistics, StatisticsSnapshot
from hopwins.capture.assembler import CirCapture
from hopwins.transport.serial_reader import (
    CaptureEvent,
    ProfileEvent,
    ReplayWorker,
    SerialWorker,
    TextEvent,
    WorkerError,
    WorkerEvent,
)

pg.setConfigOptions(antialias=False, background="#f4f5f6", foreground="#202428")


class CirMonitorWindow(QtWidgets.QMainWindow):
    def __init__(
        self,
        worker: SerialWorker | ReplayWorker,
        events: queue.Queue[WorkerEvent],
        rolling_window: int,
        source_name: str,
    ) -> None:
        super().__init__()
        self._worker = worker
        self._events = events
        self._statistics = CaptureStatistics(rolling_window)
        self._latest_statistics = self._statistics.snapshot()
        self._labels: dict[str, QtWidgets.QLabel] = {}

        self.setWindowTitle(f"HopWINS CIR Monitor - {source_name}")
        self.resize(1280, 900)
        self._build_ui()

        self._timer = QtCore.QTimer(self)
        self._timer.setInterval(33)
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
        fields = [
            ("profile", "Profile"),
            ("capture", "Capture"),
            ("rf_port", "RF port"),
            ("rx_timestamp", "RX timestamp"),
            ("mcu_time", "MCU time"),
            ("reference_time", "TIM2 time"),
            ("first_path", "First path"),
            ("peak", "Peak"),
            ("rssi", "RSSI"),
            ("fp_power", "FP power"),
            ("rx_delta", "RX delta"),
            ("fpi_stats", "FPI mean/std"),
            ("rssi_stats", "RSSI mean/std"),
            ("reference_delta", "TIM2 delta"),
            ("clock", "Clock/CFO"),
            ("early_path", "Early path/DGC"),
            ("status", "RX status"),
            ("cia", "CIA registers"),
            ("path_amplitudes", "F1/F2/F3"),
            ("integrity", "Integrity"),
        ]
        for index, (key, title) in enumerate(fields):
            row = index // 4
            column = (index % 4) * 2
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
        self._amplitude_plot = pg.PlotWidget()
        self._amplitude_plot.setLabel("left", "Accumulator")
        self._amplitude_plot.setLabel("bottom", "CIR sample")
        self._amplitude_plot.showGrid(x=True, y=True, alpha=0.2)
        self._i_curve = self._amplitude_plot.plot(
            pen=pg.mkPen("#1261a0", width=1), name="I"
        )
        self._q_curve = self._amplitude_plot.plot(
            pen=pg.mkPen("#b43b32", width=1), name="Q"
        )
        self._amplitude_plot.addLegend(offset=(8, 8))

        self._magnitude_plot = pg.PlotWidget()
        self._magnitude_plot.setLabel("left", "Magnitude", units="dB rel.")
        self._magnitude_plot.setLabel("bottom", "CIR sample")
        self._magnitude_plot.setYRange(-80.0, 2.0)
        self._magnitude_plot.showGrid(x=True, y=True, alpha=0.2)
        self._magnitude_curve = self._magnitude_plot.plot(
            pen=pg.mkPen("#18845b", width=1.5)
        )

        self._amplitude_first_path = pg.InfiniteLine(
            angle=90, pen=pg.mkPen("#8c4fb0", width=1.5)
        )
        self._amplitude_peak = pg.InfiniteLine(
            angle=90, pen=pg.mkPen("#d28416", width=1.2)
        )
        self._magnitude_first_path = pg.InfiniteLine(
            angle=90, pen=pg.mkPen("#8c4fb0", width=1.5)
        )
        self._magnitude_peak = pg.InfiniteLine(
            angle=90, pen=pg.mkPen("#d28416", width=1.2)
        )
        self._amplitude_plot.addItem(self._amplitude_first_path)
        self._amplitude_plot.addItem(self._amplitude_peak)
        self._magnitude_plot.addItem(self._magnitude_first_path)
        self._magnitude_plot.addItem(self._magnitude_peak)

        splitter.addWidget(self._amplitude_plot)
        splitter.addWidget(self._magnitude_plot)
        splitter.setSizes([430, 310])
        layout.addWidget(splitter, stretch=1)

        self._console = QtWidgets.QPlainTextEdit()
        self._console.setReadOnly(True)
        self._console.setMaximumBlockCount(150)
        self._console.setMaximumHeight(120)
        self._console.setStyleSheet(
            "font-family: Menlo, Consolas, monospace; font-size: 11px;"
        )
        layout.addWidget(self._console)

        self.setCentralWidget(central)
        self.statusBar().showMessage("Waiting for data")

    @QtCore.Slot()
    def _drain_events(self) -> None:
        latest_capture: CirCapture | None = None
        for _ in range(512):
            try:
                event = self._events.get_nowait()
            except queue.Empty:
                break
            if isinstance(event, CaptureEvent):
                latest_capture = event.capture
                self._latest_statistics = self._statistics.add(event.capture)
            elif isinstance(event, ProfileEvent):
                self._labels["profile"].setText(
                    f"{event.profile.board} / {event.profile.role}"
                )
            elif isinstance(event, TextEvent):
                self._console.appendPlainText(event.line)
            elif isinstance(event, WorkerError):
                self.statusBar().showMessage(event.message)
                self._console.appendPlainText(f"ERROR: {event.message}")

        if latest_capture is not None:
            self._update_capture(latest_capture, self._latest_statistics)

    def _update_capture(
        self,
        capture: CirCapture,
        statistics: StatisticsSnapshot,
    ) -> None:
        header = capture.header
        self._labels["capture"].setText(str(header.capture_id))
        self._labels["rf_port"].setText(
            f"{header.rf_port or '?'} (delay 0x{header.rx_antenna_delay:04X})"
        )
        self._labels["rx_timestamp"].setText(f"0x{header.rx_timestamp:010X}")
        self._labels["mcu_time"].setText(f"{header.mcu_system_time_ms} ms")
        reference = (
            f"{header.reference_time_ms} ms"
            if header.reference_time_source
            else "not available"
        )
        self._labels["reference_time"].setText(reference)
        self._labels["first_path"].setText(f"{header.first_path_index:.3f}")
        self._labels["peak"].setText(str(header.peak_index))
        self._labels["rssi"].setText(_format_dbm(header.rssi_dbm))
        self._labels["fp_power"].setText(
            _format_dbm(header.first_path_power_dbm)
        )
        self._labels["rx_delta"].setText(
            _format_mean_std(
                statistics.rx_delta_us_mean,
                statistics.rx_delta_us_std,
                "us",
            )
        )
        self._labels["fpi_stats"].setText(
            _format_mean_std(
                statistics.first_path_mean,
                statistics.first_path_std,
                "",
            )
        )
        self._labels["rssi_stats"].setText(
            _format_mean_std(
                statistics.rssi_mean_dbm,
                statistics.rssi_std_dbm,
                "dBm",
            )
        )
        self._labels["reference_delta"].setText(
            "-"
            if statistics.reference_delta_ms_mean is None
            else f"{statistics.reference_delta_ms_mean:.3f} ms mean"
        )
        self._labels["clock"].setText(
            f"offset={header.clock_offset} carrier={header.carrier_integrator}"
        )
        self._labels["early_path"].setText(
            f"{header.early_first_path_index:.3f} / "
            f"{header.early_first_path_confidence_q0_4} / "
            f"DGC {header.dgc_decision}"
        )
        self._labels["status"].setText(
            f"SYS=0x{header.system_status_low:08X}/"
            f"{header.system_status_high:08X} FINFO=0x{header.rx_finfo:08X}"
        )
        self._labels["cia"].setText(
            f"0x{header.cia_diag_0:08X} / 0x{header.cia_diag_1:08X}"
        )
        self._labels["path_amplitudes"].setText(
            f"{header.first_path_amplitude_1} / "
            f"{header.first_path_amplitude_2} / "
            f"{header.first_path_amplitude_3}"
        )
        parser = self._worker.parser.statistics
        assembler = self._worker.assembler.statistics
        self._labels["integrity"].setText(
            f"CRC={parser.crc_errors} incomplete="
            f"{assembler.incomplete_captures} invalid={assembler.invalid_chunks}"
        )
        self.statusBar().showMessage(
            f"Capture {header.capture_id}, diagnostic={header.diagnostic_status}, "
            f"CIR={header.cir_status}, registers={header.register_status}"
        )

        if not capture.has_cir:
            self._i_curve.setData([], [])
            self._q_curve.setData([], [])
            self._magnitude_curve.setData([], [])
            return

        i_values, q_values = decode_i24_q24(capture.cir_bytes)
        x_values = (
            np.arange(len(i_values), dtype=np.float64)
            + header.capture_sample_offset
        )
        self._i_curve.setData(x_values, i_values)
        self._q_curve.setData(x_values, q_values)
        self._magnitude_curve.setData(
            x_values,
            magnitude_db(i_values, q_values),
        )
        for line in (
            self._amplitude_first_path,
            self._magnitude_first_path,
        ):
            line.setValue(header.first_path_index)
        for line in (self._amplitude_peak, self._magnitude_peak):
            line.setValue(header.peak_index)

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self._timer.stop()
        self._worker.stop()
        event.accept()


def run_live_monitor(
    port: str,
    baudrate: int,
    record_path: str | Path | None,
    rolling_window: int,
    *,
    timeout_s: float = 0.1,
    expected_board: str | None = None,
    expected_role: str | None = None,
    recorder_metadata: Mapping[str, object] | None = None,
) -> int:
    events: queue.Queue[WorkerEvent] = queue.Queue(maxsize=4096)
    worker = SerialWorker(
        port,
        baudrate,
        events,
        record_path,
        timeout_s=timeout_s,
        expected_board=expected_board,
        expected_role=expected_role,
        recorder_metadata=recorder_metadata,
    )
    return _run_window(worker, events, rolling_window, port)


def run_replay_monitor(
    path: str | Path,
    speed: float,
    rolling_window: int,
) -> int:
    events: queue.Queue[WorkerEvent] = queue.Queue(maxsize=4096)
    worker = ReplayWorker(path, events, speed)
    return _run_window(worker, events, rolling_window, str(path))


def _run_window(
    worker: SerialWorker | ReplayWorker,
    events: queue.Queue[WorkerEvent],
    rolling_window: int,
    source_name: str,
) -> int:
    application = QtWidgets.QApplication.instance()
    owns_application = application is None
    if application is None:
        application = QtWidgets.QApplication(sys.argv)
    window = CirMonitorWindow(worker, events, rolling_window, source_name)
    window.show()
    if owns_application:
        return application.exec()
    return 0


def _format_dbm(value: float | None) -> str:
    return "-" if value is None else f"{value:.2f} dBm"


def _format_mean_std(
    mean: float | None,
    standard_deviation: float | None,
    unit: str,
) -> str:
    if mean is None or standard_deviation is None:
        return "-"
    suffix = f" {unit}" if unit else ""
    return f"{mean:.3f} / {standard_deviation:.3f}{suffix}"
