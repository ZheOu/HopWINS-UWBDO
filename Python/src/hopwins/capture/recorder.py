"""Lossless raw stream recorder with portable JSON session metadata."""

from __future__ import annotations

import json
from collections.abc import Mapping
from datetime import UTC, datetime
from pathlib import Path
from typing import BinaryIO

from hopwins.devices.profiles import FirmwareProfile


class RawSessionRecorder:
    def __init__(
        self,
        path: str | Path,
        session_metadata: Mapping[str, object] | None = None,
    ) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file: BinaryIO = self.path.open("wb")
        self._started = datetime.now(UTC)
        self._bytes_written = 0
        self._profile: FirmwareProfile | None = None
        self._session_metadata = dict(session_metadata or {})

    def write(self, data: bytes) -> None:
        self._file.write(data)
        self._bytes_written += len(data)

    def set_profile(self, profile: FirmwareProfile) -> None:
        self._profile = profile

    def close(self) -> None:
        if self._file.closed:
            return
        self._file.flush()
        self._file.close()
        finished = datetime.now(UTC)
        metadata: dict[str, object] = {
            "format": "raw-hcir-stream",
            "started_utc": self._started.isoformat(),
            "finished_utc": finished.isoformat(),
            "bytes": self._bytes_written,
        }
        if self._session_metadata:
            metadata["session"] = self._session_metadata
        if self._profile is not None:
            metadata["firmware_profile"] = {
                "board": self._profile.board,
                "role": self._profile.role,
                "build": self._profile.build,
                "fpga": self._profile.has_fpga,
                "clock": self._profile.has_clock_control,
                "external_timer": self._profile.has_external_timer,
            }
        metadata_path = self.path.with_suffix(self.path.suffix + ".json")
        metadata_path.write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def __enter__(self) -> RawSessionRecorder:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
