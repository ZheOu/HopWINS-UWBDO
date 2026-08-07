"""Common key/value diagnostic text records emitted by MCU firmware."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class FirmwareProfile:
    board: str
    role: str
    build: str
    has_fpga: bool
    has_clock_control: bool
    has_external_timer: bool

    @property
    def supports_cir(self) -> bool:
        return self.role == "DO-Follower"


def parse_fields(line: str) -> dict[str, str]:
    """Parse the firmware's stable `PREFIX, KEY=VALUE` text convention."""
    fields: dict[str, str] = {}
    for item in line.split(",")[1:]:
        key, separator, value = item.strip().partition("=")
        if separator and key != "CRC32":
            fields[key] = value
    return fields


def parse_firmware_profile(line: str) -> FirmwareProfile | None:
    if not line.startswith("FW PROFILE,"):
        return None
    fields = parse_fields(line)
    required = {"BOARD", "ROLE", "BUILD", "FPGA", "CLOCK", "EXT_TIMER"}
    if not required.issubset(fields):
        return None
    return FirmwareProfile(
        board=fields["BOARD"],
        role=fields["ROLE"],
        build=fields["BUILD"],
        has_fpga=fields["FPGA"] == "1",
        has_clock_control=fields["CLOCK"] == "1",
        has_external_timer=fields["EXT_TIMER"] == "1",
    )
