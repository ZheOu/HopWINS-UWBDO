"""NumPy conversion helpers for DW3000 signed 24-bit complex samples."""

from __future__ import annotations

import numpy as np
from numpy.typing import NDArray


def decode_i24_q24(data: bytes) -> tuple[NDArray[np.int32], NDArray[np.int32]]:
    if len(data) % 6:
        raise ValueError("I24/Q24 CIR payload length must be divisible by six")

    packed = np.frombuffer(data, dtype=np.uint8).reshape((-1, 6))
    i_values = (
        packed[:, 0].astype(np.int32)
        | (packed[:, 1].astype(np.int32) << 8)
        | (packed[:, 2].astype(np.int32) << 16)
    )
    q_values = (
        packed[:, 3].astype(np.int32)
        | (packed[:, 4].astype(np.int32) << 8)
        | (packed[:, 5].astype(np.int32) << 16)
    )
    i_values = np.where(i_values & 0x800000, i_values - 0x1000000, i_values)
    q_values = np.where(q_values & 0x800000, q_values - 0x1000000, q_values)
    return i_values.astype(np.int32), q_values.astype(np.int32)


def magnitude_db(
    i_values: NDArray[np.int32],
    q_values: NDArray[np.int32],
) -> NDArray[np.float64]:
    magnitude = np.hypot(i_values.astype(np.float64), q_values.astype(np.float64))
    reference = max(float(magnitude.max(initial=0.0)), 1.0)
    return 20.0 * np.log10(np.maximum(magnitude, 1.0) / reference)
