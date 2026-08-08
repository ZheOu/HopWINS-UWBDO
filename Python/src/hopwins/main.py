"""Thin command-line entry point for all HopWINS tasks."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence

from hopwins.config import ConfigurationError, ProjectConfig
from hopwins.core.prompts import resolve_task_inputs
from hopwins.registry import TASKS, run_task, task_definitions, task_prompt_spec


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="hopwins")
    commands = parser.add_subparsers(dest="command", required=True)

    tasks = commands.add_parser("tasks", help="list registered tasks")
    tasks.add_argument("--category")

    run = commands.add_parser("run", help="run one registered task")
    run.add_argument("task", nargs="?")
    _add_runtime_arguments(run)

    diagnose = commands.add_parser("diagnose", help="run a diagnostic task")
    diagnose.add_argument("task")
    _add_runtime_arguments(diagnose)

    commands.add_parser("ports", help="list serial ports")
    return parser


def main(arguments: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(arguments)
    if args.command == "tasks":
        for spec in task_definitions(args.category):
            modes = "/".join(
                mode
                for mode, enabled in (
                    ("online", spec.supports_online),
                    ("offline", spec.supports_offline),
                )
                if enabled
            )
            print(f"{spec.name:18} {spec.category:12} {modes:14} {spec.description}")
        return 0
    if args.command == "ports":
        try:
            config = ProjectConfig.load()
            return run_task(config, task_name="list_ports")
        except (ConfigurationError, OSError, RuntimeError, ValueError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2

    try:
        config = ProjectConfig.load(args.config)
        task_name = args.task or config.task_name
        if args.command == "diagnose":
            task_name = _diagnostic_name(args.task)
        input_path = config.resolve_path(args.input) if args.input else None
        overrides = _parse_overrides(args.parameter)
        if args.duration is not None:
            overrides["duration_s"] = args.duration
        prompt_spec = task_prompt_spec(task_name)
        if args.prompt and prompt_spec is None:
            raise ValueError(f"task {task_name!r} does not declare interactive prompts")
        if args.prompt and prompt_spec is not None:
            resolved = resolve_task_inputs(
                prompt_spec,
                label=args.label,
                notes=args.notes,
                effective_parameters=config.task_parameters(task_name, overrides),
                parameter_overrides=overrides,
                interactive=args.prompt,
            )
            label = resolved.label
            notes = resolved.notes
            overrides = resolved.parameter_overrides
        else:
            label = args.label
            notes = args.notes
        return run_task(
            config,
            task_name=task_name,
            device_name=args.device,
            mode=args.mode,
            input_path=input_path,
            label=label,
            notes=notes,
            parameter_overrides=overrides,
        )
    except (ConfigurationError, OSError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


def _add_runtime_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--config")
    parser.add_argument("--device")
    parser.add_argument("--mode", choices=("online", "offline"), default="online")
    parser.add_argument("--input")
    parser.add_argument("--label")
    parser.add_argument("--notes", "--note", dest="notes")
    parser.add_argument("--duration", type=float)
    parser.add_argument(
        "--prompt",
        action="store_true",
        help="interactively confirm task metadata and selected parameters",
    )
    parser.add_argument(
        "--param",
        dest="parameter",
        action="append",
        default=[],
        metavar="KEY=VALUE",
    )


def _parse_overrides(items: list[str]) -> dict[str, object]:
    values: dict[str, object] = {}
    for item in items:
        key, separator, raw = item.partition("=")
        if not separator or not key.strip():
            raise ValueError(f"invalid --param {item!r}; expected KEY=VALUE")
        values[key.strip()] = _coerce_value(raw.strip())
    return values


def _coerce_value(value: str) -> object:
    folded = value.casefold()
    if folded in {"true", "false"}:
        return folded == "true"
    try:
        return int(value, 0)
    except ValueError:
        try:
            return float(value)
        except ValueError:
            return value


def _diagnostic_name(name: str) -> str:
    candidates = (name, f"diagnostic.{name}", f"diag_{name}")
    for candidate in candidates:
        spec = TASKS.get(candidate)
        if spec is not None and spec.category == "diagnostic":
            return candidate
    raise ValueError(f"unknown diagnostic task: {name}")


if __name__ == "__main__":
    raise SystemExit(main())
