from __future__ import annotations

from hopwins.cli import build_parser


def test_registered_task_names_are_direct_commands() -> None:
    parser = build_parser()

    probe = parser.parse_args(["serial_probe", "--device", "leader"])
    ports = parser.parse_args(["list_ports"])
    spatial = parser.parse_args(["spatial_timing_analysis"])

    assert probe.command == "serial_probe"
    assert probe.device == "leader"
    assert ports.command == "list_ports"
    assert spatial.command == "spatial_timing_analysis"


def test_replay_supports_configured_and_legacy_forms() -> None:
    parser = build_parser()

    configured = parser.parse_args(["replay"])
    legacy = parser.parse_args(["replay", "captures/example.hcir"])

    assert configured.path is None
    assert legacy.path == "captures/example.hcir"
