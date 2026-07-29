"""Rolling statistics for timing and first-path channel measurements."""

from __future__ import annotations

import math
from collections import deque
from dataclasses import dataclass
from statistics import fmean, pstdev

from hopwins.capture.assembler import CirCapture
from hopwins.protocol.packets import PacketFlags

DW_TIME_UNIT_SECONDS = 1.0 / (499.2e6 * 128.0)
RX_TIMESTAMP_MASK = (1 << 40) - 1


@dataclass(frozen=True, slots=True)
class StatisticsSnapshot:
    captures: int
    rx_delta_us_mean: float | None
    rx_delta_us_std: float | None
    first_path_mean: float | None
    first_path_std: float | None
    rssi_mean_dbm: float | None
    rssi_std_dbm: float | None
    reference_delta_ms_mean: float | None


class CaptureStatistics:
    def __init__(self, window: int = 200) -> None:
        self._rx_delta_us: deque[float] = deque(maxlen=window)
        self._first_path: deque[float] = deque(maxlen=window)
        self._rssi: deque[float] = deque(maxlen=window)
        self._reference_delta_ms: deque[float] = deque(maxlen=window)
        self._previous_rx_timestamp: int | None = None
        self._previous_reference_time: int | None = None
        self.captures = 0

    def add(self, capture: CirCapture) -> StatisticsSnapshot:
        header = capture.header
        if self._previous_rx_timestamp is not None:
            delta = (
                header.rx_timestamp - self._previous_rx_timestamp
            ) & RX_TIMESTAMP_MASK
            self._rx_delta_us.append(delta * DW_TIME_UNIT_SECONDS * 1e6)
        self._previous_rx_timestamp = header.rx_timestamp

        if header.first_path_index_q10_6:
            self._first_path.append(header.first_path_index)
        if header.rssi_dbm is not None:
            self._rssi.append(header.rssi_dbm)

        if header.reference_time_source and (
            header.flags & PacketFlags.REFERENCE_TIME_VALID
        ):
            if self._previous_reference_time is not None:
                delta_ms = (
                    header.reference_time_ms - self._previous_reference_time
                ) & 0xFFFFFFFF
                self._reference_delta_ms.append(float(delta_ms))
            self._previous_reference_time = header.reference_time_ms

        self.captures += 1
        return self.snapshot()

    def snapshot(self) -> StatisticsSnapshot:
        return StatisticsSnapshot(
            captures=self.captures,
            rx_delta_us_mean=_mean(self._rx_delta_us),
            rx_delta_us_std=_std(self._rx_delta_us),
            first_path_mean=_mean(self._first_path),
            first_path_std=_std(self._first_path),
            rssi_mean_dbm=_mean(self._rssi),
            rssi_std_dbm=_std(self._rssi),
            reference_delta_ms_mean=_mean(self._reference_delta_ms),
        )


def _mean(values: deque[float]) -> float | None:
    return fmean(values) if values else None


def _std(values: deque[float]) -> float | None:
    if not values:
        return None
    value = pstdev(values)
    return value if math.isfinite(value) else None
