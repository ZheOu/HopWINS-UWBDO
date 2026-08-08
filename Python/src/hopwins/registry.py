"""Explicit task registry and dispatch."""

from __future__ import annotations

import importlib
from pathlib import Path

from hopwins.config import ProjectConfig
from hopwins.core.prompts import TaskPromptSpec, resolve_task_inputs
from hopwins.core.task import TaskContext, TaskSpec

TASKS: dict[str, TaskSpec] = {
    spec.name: spec
    for spec in (
        TaskSpec(
            "session_capture",
            "capture",
            "record a reproducible online or offline experiment session",
            "hopwins.tasks.session_capture",
            requires_device=True,
            supports_offline=True,
            protocol="hcir",
        ),
        TaskSpec(
            "do_leader_dataset",
            "dataset",
            "organize DO-Leader TX and service records into timestamped tables",
            "hopwins.tasks.do_leader_dataset",
            requires_device=True,
            supports_offline=True,
            protocol="hcir",
        ),
        TaskSpec(
            "do_follower_dataset",
            "dataset",
            "organize DO tracking, RX health, and optional CIR captures",
            "hopwins.tasks.do_follower_dataset",
            requires_device=True,
            supports_offline=True,
            protocol="hcir",
        ),
        TaskSpec(
            "sts_tx_dataset",
            "dataset",
            "organize STS diagnostic TX events into a timestamped dataset",
            "hopwins.tasks.sts_tx_dataset",
            requires_device=True,
            supports_offline=True,
            protocol="hcir",
        ),
        TaskSpec(
            "cir_monitor",
            "interactive",
            "display live CIR data and optionally record the raw stream",
            "hopwins.tasks.cir_monitor",
            requires_device=True,
            protocol="hcir_v2",
        ),
        TaskSpec(
            "dual_cir_monitor",
            "diagnostic",
            "record or replay dynamic HCIR v3 STS0/STS1 CIR pairs",
            "hopwins.tasks.dual_cir_monitor",
            requires_device=True,
            supports_offline=True,
            protocol="hcir_v3",
        ),
        TaskSpec(
            "dual_cir_dataset",
            "diagnostic",
            "record algorithm-ready HCIR v3 STS0/STS1 paired CIR data",
            "hopwins.tasks.dual_cir_dataset",
            requires_device=True,
            supports_offline=True,
            protocol="hcir_v3",
        ),
        TaskSpec(
            "raw_record",
            "capture",
            "legacy single-file lossless serial recording",
            "hopwins.tasks.raw_record",
            requires_device=True,
            protocol="hcir_v2",
        ),
        TaskSpec(
            "capture_inspect",
            "analysis",
            "print RX metadata and a small CIR window",
            "hopwins.tasks.capture_inspect",
            supports_online=False,
            supports_offline=True,
            protocol="hcir_v2",
        ),
        TaskSpec(
            "replay",
            "interactive",
            "replay a recorded HCIR stream in the CIR monitor",
            "hopwins.tasks.replay",
            supports_online=False,
            supports_offline=True,
            protocol="hcir_v2",
        ),
        TaskSpec(
            "list_ports",
            "utility",
            "list serial ports and hardware identifiers",
            "hopwins.tasks.list_ports",
        ),
    )
}


def run_task(
    config: ProjectConfig,
    *,
    task_name: str | None = None,
    device_name: str | None = None,
    mode: str = "online",
    input_path: Path | None = None,
    label: str | None = None,
    notes: str | None = None,
    parameter_overrides: dict[str, object] | None = None,
) -> int:
    selected_name = task_name or config.task_name
    spec = TASKS.get(selected_name)
    if spec is None:
        choices = ", ".join(TASKS)
        raise ValueError(f"unknown task {selected_name!r}; available: {choices}")
    if mode == "online" and not spec.supports_online:
        raise ValueError(f"task {selected_name!r} does not support online mode")
    if mode == "offline" and not spec.supports_offline:
        raise ValueError(f"task {selected_name!r} does not support offline mode")
    selected_device = device_name or config.device_name
    if spec.requires_device and mode == "online" and not selected_device:
        raise ValueError(f"task {selected_name!r} requires a configured device")

    overrides = dict(parameter_overrides or {})
    module = importlib.import_module(spec.module)
    prompt = _module_prompt(spec, module)
    if prompt is not None:
        resolved = resolve_task_inputs(
            prompt,
            label=label,
            notes=notes,
            effective_parameters=config.task_parameters(selected_name, overrides),
            parameter_overrides=overrides,
            interactive=False,
        )
        label = resolved.label
        notes = resolved.notes
        overrides = resolved.parameter_overrides

    context = TaskContext(
        config=config,
        spec=spec,
        device_name=selected_device,
        mode=mode,
        input_path=input_path,
        label=label,
        notes=notes,
        parameter_overrides=overrides,
    )
    runner = getattr(module, "run_configured", None)
    if not callable(runner):
        raise RuntimeError(f"task module {spec.module} has no run_configured()")
    return int(runner(context))


def task_definitions(category: str | None = None) -> tuple[TaskSpec, ...]:
    values = tuple(TASKS.values())
    if category is None:
        return values
    return tuple(spec for spec in values if spec.category == category)


def task_prompt_spec(task_name: str) -> TaskPromptSpec | None:
    """Load the optional prompt declaration kept beside a task's code."""

    spec = TASKS.get(task_name)
    if spec is None:
        choices = ", ".join(TASKS)
        raise ValueError(f"unknown task {task_name!r}; available: {choices}")
    module = importlib.import_module(spec.module)
    return _module_prompt(spec, module)


def _module_prompt(spec: TaskSpec, module: object) -> TaskPromptSpec | None:
    prompt = getattr(module, "PROMPT", None)
    if prompt is not None and not isinstance(prompt, TaskPromptSpec):
        raise RuntimeError(f"task module {spec.module} has an invalid PROMPT")
    return prompt
