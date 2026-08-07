"""Pair the two STS CIR records emitted for one HCIR v3 capture ID."""

from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass, field

from hopwins.capture.assembler import CirCapture
from hopwins.protocol.packets import CirSource


@dataclass(frozen=True, slots=True)
class DualCirCapture:
    capture_id: int
    sts0: CirCapture
    sts1: CirCapture


@dataclass(slots=True)
class PairingStatistics:
    completed_pairs: int = 0
    incomplete_pairs: int = 0
    duplicate_channels: int = 0
    invalid_channels: int = 0


@dataclass(slots=True)
class _PendingPair:
    channels: dict[CirSource, CirCapture] = field(default_factory=dict)


class DualCirPairAssembler:
    def __init__(self, maximum_pending: int = 8) -> None:
        self._pending: OrderedDict[int, _PendingPair] = OrderedDict()
        self._maximum_pending = maximum_pending
        self.statistics = PairingStatistics()

    def add(self, capture: CirCapture) -> DualCirCapture | None:
        header = capture.header
        if (
            header.version < 3
            or header.cir_group_size != 2
            or header.cir_source not in (CirSource.STS0, CirSource.STS1)
        ):
            self.statistics.invalid_channels += 1
            return None

        pending = self._pending.setdefault(header.capture_id, _PendingPair())
        self._pending.move_to_end(header.capture_id)
        if header.cir_source in pending.channels:
            self.statistics.duplicate_channels += 1
            return None
        pending.channels[header.cir_source] = capture

        if CirSource.STS0 in pending.channels and CirSource.STS1 in pending.channels:
            del self._pending[header.capture_id]
            self.statistics.completed_pairs += 1
            return DualCirCapture(
                capture_id=header.capture_id,
                sts0=pending.channels[CirSource.STS0],
                sts1=pending.channels[CirSource.STS1],
            )

        self._evict_old_pairs()
        return None

    def _evict_old_pairs(self) -> None:
        while len(self._pending) > self._maximum_pending:
            self._pending.popitem(last=False)
            self.statistics.incomplete_pairs += 1
