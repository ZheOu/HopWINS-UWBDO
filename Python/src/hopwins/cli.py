"""HopWINS host command-line entry point."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence

from hopwins.config import ConfigurationError, ProjectConfig

DEFAULT_BAUDRATE = 5_000_000


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="hopwins")
    subparsers = parser.add_subparsers(dest="command", required=True)

    run = subparsers.add_parser(
        "run",
        help="run the task selected by config.ini",
    )
    run.add_argument("--config")
    run.add_argument("--task", help="override [app] task")
    run.add_argument("--device", help="override [app] device")

    subparsers.add_parser("tasks", help="list registered task names")
    subparsers.add_parser("ports", help="list available serial ports")

    monitor = subparsers.add_parser("monitor", help="show the live CIR monitor")
    monitor.add_argument("--port", required=True)
    monitor.add_argument("--baud", type=int, default=DEFAULT_BAUDRATE)
    monitor.add_argument("--record")
    monitor.add_argument("--window", type=int, default=200)

    record = subparsers.add_parser("record", help="record a raw HCIR session")
    record.add_argument("--port", required=True)
    record.add_argument("--baud", type=int, default=DEFAULT_BAUDRATE)
    record.add_argument("--output", required=True)

    replay = subparsers.add_parser("replay", help="replay a recorded session")
    replay.add_argument("path")
    replay.add_argument("--speed", type=float, default=1.0)
    replay.add_argument("--window", type=int, default=200)
    return parser


def main(arguments: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(arguments)
    if args.command == "run":
        from hopwins.tasks.registry import run_configured_task

        try:
            config = ProjectConfig.load(args.config)
            return run_configured_task(
                config,
                task_name=args.task,
                device_name=args.device,
            )
        except (ConfigurationError, OSError, RuntimeError, ValueError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2
    if args.command == "tasks":
        from hopwins.tasks.registry import task_definitions

        for task in task_definitions():
            device = "device" if task.requires_device else "offline"
            print(f"{task.name:16} {device:7} {task.description}")
        return 0
    if args.command == "ports":
        from hopwins.tasks.list_ports import run

        return run()
    if args.command == "monitor":
        from hopwins.tasks.cir_monitor import run

        return run(args.port, args.baud, args.record, args.window)
    if args.command == "record":
        from hopwins.tasks.raw_record import run

        return run(args.port, args.baud, args.output)
    if args.command == "replay":
        from hopwins.tasks.replay import run

        return run(args.path, args.speed, args.window)
    return 2
