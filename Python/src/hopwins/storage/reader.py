"""Resolve recorded session directories and legacy HCIR files."""

from __future__ import annotations

from pathlib import Path


def resolve_raw_stream(path: str | Path) -> Path:
    candidate = Path(path).expanduser().resolve()
    if candidate.is_dir():
        candidate = candidate / "raw" / "serial.bin"
    if not candidate.is_file():
        raise ValueError(f"recorded serial stream not found: {candidate}")
    return candidate
