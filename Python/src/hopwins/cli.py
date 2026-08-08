"""HopWINS host command-line entry point."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence

from hopwins.config import ConfigurationError, ProjectConfig
from hopwins.tasks.registry import task_definitions

DEFAULT_BAUDRATE = 5_000_000


def _add_config_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--config", help="configuration file path")
    parser.add_argument("--device", help="override [app] device")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="hopwins")
    subparsers = parser.add_subparsers(dest="command", required=True)

    run = subparsers.add_parser(
        "run",
        help="run the task selected by config.ini",
    )
    _add_config_arguments(run)
    run.add_argument("--task", help="override [app] task")

    subparsers.add_parser("tasks", help="list registered task names")

    for task in task_definitions():
        if task.name == "replay":
            continue
        task_parser = subparsers.add_parser(task.name, help=task.description)
        _add_config_arguments(task_parser)

    monitor = subparsers.add_parser("monitor", help="show the live CIR monitor")
    monitor.add_argument("--port", required=True)
    monitor.add_argument("--baud", type=int, default=DEFAULT_BAUDRATE)
    monitor.add_argument("--record")
    monitor.add_argument("--window", type=int, default=200)

    record = subparsers.add_parser("record", help="record a raw HCIR session")
    record.add_argument("--port", required=True)
    record.add_argument("--baud", type=int, default=DEFAULT_BAUDRATE)
    record.add_argument("--output", required=True)

    replay = subparsers.add_parser(
        "replay",
        help="run configured replay, or replay a specified capture",
    )
    _add_config_arguments(replay)
    replay.add_argument("path", nargs="?")
    replay.add_argument("--speed", type=float, default=1.0)
    replay.add_argument("--window", type=int, default=200)

    subparsers.add_parser("ports", help="legacy alias for list_ports")
    return parser


def _run_configured(
    args: argparse.Namespace,
    *,
    task_name: str | None = None,
) -> int:
    from hopwins.tasks.registry import run_configured_task

    try:
        config = ProjectConfig.load(args.config)
        return run_configured_task(
            config,
            task_name=task_name,
            device_name=args.device,
        )
    except (ConfigurationError, OSError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


def main(arguments: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(arguments)
    if args.command == "run":
        return _run_configured(args, task_name=args.task)
    if args.command == "tasks":
        definitions = task_definitions()
        name_width = max(len(task.name) for task in definitions)
        for task in definitions:
            device = "device" if task.requires_device else "offline"
            print(f"{task.name:<{name_width}}  {device:7} {task.description}")
        return 0
    registered_commands = {task.name for task in task_definitions()}
    if args.command in registered_commands:
        if args.command == "replay" and args.path is not None:
            from hopwins.tasks.replay import run

            return run(args.path, args.speed, args.window)
        return _run_configured(args, task_name=args.command)
    if args.command == "ports":
        from hopwins.tasks.list_ports import run

        return run()
    if args.command == "monitor":
        from hopwins.tasks.cir_monitor import run

        return run(args.port, args.baud, args.record, args.window)
    if args.command == "record":
        from hopwins.tasks.raw_record import run

        return run(args.port, args.baud, args.output)
    return 2
