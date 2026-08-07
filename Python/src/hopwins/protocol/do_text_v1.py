"""Typed parsing for DO tracking configuration and update records."""

from __future__ import annotations

from dataclasses import dataclass

from hopwins.protocol.common_text import parse_fields


@dataclass(frozen=True, slots=True)
class DoTrackConfig:
    enabled: bool
    ready: bool
    loop: str
    timestamp: str
    window: int
    maximum_error_ppb: int
    command_limit_ppb: int
    capture_output: str
    cir_capture: bool


@dataclass(frozen=True, slots=True)
class DoTrackRecord:
    result: str
    reference_sequence: int
    sequence: int
    interval_count: int
    leader_delta_dtu: int
    follower_delta_dtu: int
    error_ppb: int | None
    command_ppb: int
    clock_status: int
    observations: int
    updates: int
    rejects: int


def parse_do_track_config(line: str) -> DoTrackConfig | None:
    if not line.startswith("DO TRACK CFG,"):
        return None
    fields = parse_fields(line)
    required = {
        "ENABLE",
        "READY",
        "LOOP",
        "TIMESTAMP",
        "WINDOW",
        "MAX_ERR_PPB",
        "CMD_LIMIT_PPB",
        "CAPTURE_OUTPUT",
        "CIR_CAPTURE",
    }
    if not required.issubset(fields):
        return None
    try:
        return DoTrackConfig(
            enabled=_boolean(fields["ENABLE"]),
            ready=_boolean(fields["READY"]),
            loop=fields["LOOP"],
            timestamp=fields["TIMESTAMP"],
            window=_integer(fields["WINDOW"]),
            maximum_error_ppb=_integer(fields["MAX_ERR_PPB"]),
            command_limit_ppb=_integer(fields["CMD_LIMIT_PPB"]),
            capture_output=fields["CAPTURE_OUTPUT"],
            cir_capture=_boolean(fields["CIR_CAPTURE"]),
        )
    except ValueError:
        return None


def parse_do_track(line: str) -> DoTrackRecord | None:
    if not line.startswith("DO TRACK,"):
        return None
    fields = parse_fields(line)
    required = {
        "RESULT",
        "SEQ0",
        "SEQ1",
        "N",
        "TX_DT",
        "RX_DT",
        "ERR_PPB",
        "CMD_PPB",
        "CLK_STATUS",
        "OBS",
        "UPDATES",
        "REJECT",
    }
    if not required.issubset(fields):
        return None
    try:
        error = (
            None
            if fields["ERR_PPB"] == "OUT_OF_RANGE"
            else _integer(fields["ERR_PPB"])
        )
        return DoTrackRecord(
            result=fields["RESULT"],
            reference_sequence=_integer(fields["SEQ0"]),
            sequence=_integer(fields["SEQ1"]),
            interval_count=_integer(fields["N"]),
            leader_delta_dtu=_integer(fields["TX_DT"]),
            follower_delta_dtu=_integer(fields["RX_DT"]),
            error_ppb=error,
            command_ppb=_integer(fields["CMD_PPB"]),
            clock_status=_integer(fields["CLK_STATUS"]),
            observations=_integer(fields["OBS"]),
            updates=_integer(fields["UPDATES"]),
            rejects=_integer(fields["REJECT"]),
        )
    except ValueError:
        return None


def _integer(value: str) -> int:
    if value.startswith(("0x", "-0x", "+0x")):
        return int(value, 0)
    return int(value, 10)


def _boolean(value: str) -> bool:
    if value == "1":
        return True
    if value == "0":
        return False
    raise ValueError("expected firmware boolean")
