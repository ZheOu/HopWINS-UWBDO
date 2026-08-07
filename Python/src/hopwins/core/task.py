"""Task declarations and runtime context."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING, Protocol

if TYPE_CHECKING:
    from hopwins.config import DeviceConfig, ProjectConfig
    from hopwins.core.records import Record
    from hopwins.core.session import ExperimentSession
    from hopwins.storage.recorder import SessionRecorder


@dataclass(frozen=True, slots=True)
class TaskSpec:
    name: str
    category: str
    description: str
    module: str
    requires_device: bool = False
    supports_online: bool = True
    supports_offline: bool = False
    protocol: str | None = None


@dataclass(slots=True)
class TaskContext:
    config: ProjectConfig
    spec: TaskSpec
    device_name: str | None = None
    mode: str = "online"
    input_path: Path | None = None
    label: str | None = None
    notes: str | None = None
    parameter_overrides: dict[str, object] = field(default_factory=dict)
    session: ExperimentSession | None = None
    recorder: SessionRecorder | None = None

    @property
    def task_name(self) -> str:
        return self.spec.name

    @property
    def parameters(self) -> dict[str, object]:
        return self.config.task_parameters(
            self.task_name,
            self.parameter_overrides,
        )

    def parameter_text(self, name: str, *, fallback: str = "") -> str:
        return str(self.parameters.get(name, fallback)).strip()

    def parameter_bool(self, name: str, *, fallback: bool) -> bool:
        value = self.parameters.get(name, fallback)
        if isinstance(value, bool):
            return value
        raise ValueError(f"task parameter {name!r} must be a boolean")

    def parameter_int(self, name: str, *, fallback: int) -> int:
        value = self.parameters.get(name, fallback)
        if isinstance(value, bool):
            raise ValueError(f"task parameter {name!r} must be an integer")
        if isinstance(value, int):
            return value
        try:
            return int(str(value), 0)
        except ValueError as exc:
            raise ValueError(
                f"task parameter {name!r} must be an integer"
            ) from exc

    def parameter_float(self, name: str, *, fallback: float) -> float:
        value = self.parameters.get(name, fallback)
        if isinstance(value, bool):
            raise ValueError(f"task parameter {name!r} must be a number")
        try:
            return float(value)
        except (TypeError, ValueError) as exc:
            raise ValueError(f"task parameter {name!r} must be a number") from exc

    def require_device(self) -> DeviceConfig:
        return self.config.device(self.device_name)

    def resolve_port(self, device: DeviceConfig) -> str:
        from hopwins.io.serial import resolve_serial_port

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
            "parameters": self.parameters,
            "serial": {
                "port": resolved_port,
                "baudrate": device.baudrate,
                "timeout_s": device.timeout_s,
            },
        }


class RecordTask(Protocol):
    def start(self, context: TaskContext) -> None: ...

    def handle(self, record: Record, context: TaskContext) -> None: ...

    def stop(self, context: TaskContext) -> dict[str, object]: ...
