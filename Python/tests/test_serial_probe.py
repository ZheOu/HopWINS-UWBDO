from __future__ import annotations

import pytest

from hopwins.tasks.serial_probe import format_hex_preview, run


def test_hex_preview_contains_offsets_hex_and_printable_text() -> None:
    preview = format_hex_preview(b"HWDO\x00\xff")

    assert preview == (
        "00000000  48 57 44 4F 00 FF                                |HWDO..|"
    )


def test_probe_rejects_invalid_settings_before_opening_port() -> None:
    with pytest.raises(ValueError, match="duration_s"):
        run("unused", 5_000_000, duration_s=-1)
