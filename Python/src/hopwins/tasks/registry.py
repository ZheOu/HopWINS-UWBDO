"""Explicit registry and configured entry points for host-side tasks."""

from __future__ import annotations

import importlib
from dataclasses import dataclass

from hopwins.config import DeviceConfig, ProjectConfig
from hopwins.transport.serial_reader import resolve_serial_port


@dataclass(frozen=True, slots=True)
class TaskDefinition:
    name: str
    description: str
    requires_device: bool
    module: str


@dataclass(frozen=True, slots=True)
class TaskContext:
    config: ProjectConfig
    task_name: str
    device_name: str | None

    def require_device(self) -> DeviceConfig:
        return self.config.device(self.device_name)

    def resolve_port(self, device: DeviceConfig) -> str:
        return resolve_serial_port(
            device.port,
            vid=device.vid,
            pid=device.pid,
            serial_number=device.serial_number,
            description_contains=device.description_contains,
        )

    def recorder_metadata(
        self,
        device: DeviceConfig,
        resolved_port: str,
    ) -> dict[str, object]:
        return {
            "task": self.task_name,
            "device": device.name,
            "config_files": [str(path) for path in self.config.loaded_paths],
            "effective_config": self.config.snapshot(),
            "serial": {
                "port": resolved_port,
                "baudrate": device.baudrate,
                "timeout_s": device.timeout_s,
            },
        }


TASKS: dict[str, TaskDefinition] = {
    definition.name: definition
    for definition in (
        TaskDefinition(
            "cir_monitor",
            "display live CIR data and optionally record the raw stream",
            True,
            "hopwins.tasks.cir_monitor",
        ),
        TaskDefinition(
            "raw_record",
            "record a lossless HCIR stream without opening a GUI",
            True,
            "hopwins.tasks.raw_record",
        ),
        TaskDefinition(
            "capture_inspect",
            "print RX metadata and a small CIR window around the FPI",
            False,
            "hopwins.tasks.capture_inspect",
        ),
        TaskDefinition(
            "replay",
            "replay a recorded HCIR stream in the CIR monitor",
            False,
            "hopwins.tasks.replay",
        ),
        TaskDefinition(
            "list_ports",
            "list serial ports and hardware identifiers",
            False,
            "hopwins.tasks.list_ports",
        ),
    )
}


def run_configured_task(
    config: ProjectConfig,
    *,
    task_name: str | None = None,
    device_name: str | None = None,
) -> int:
    selected_task = task_name or config.task_name
    definition = TASKS.get(selected_task)
    if definition is None:
        choices = ", ".join(TASKS)
        raise ValueError(f"unknown task {selected_task!r}; available tasks: {choices}")
    selected_device = device_name or config.device_name
    if definition.requires_device and not selected_device:
        raise ValueError(f"task {selected_task!r} requires a configured device")

    module = importlib.import_module(definition.module)
    runner = getattr(module, "run_configured", None)
    if not callable(runner):
        raise RuntimeError(f"task module {definition.module} has no run_configured()")
    return int(runner(TaskContext(config, selected_task, selected_device)))


def task_definitions() -> tuple[TaskDefinition, ...]:
    return tuple(TASKS.values())
