"""TOML configuration for devices, tasks, and experiment storage."""

from __future__ import annotations

import copy
import os
import re
import tomllib
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


class ConfigurationError(ValueError):
    """Raised when project configuration is missing or invalid."""


@dataclass(frozen=True, slots=True)
class DeviceConfig:
    name: str
    port: str
    baudrate: int
    timeout_s: float
    vid: int | None
    pid: int | None
    serial_number: str | None
    description_contains: str | None
    expected_board: str | None
    expected_role: str | None


class ProjectConfig:
    """Effective configuration loaded from shared and local TOML files."""

    def __init__(
        self,
        path: Path,
        data: dict[str, Any],
        loaded_paths: tuple[Path, ...],
    ) -> None:
        self.path = path
        self._data = data
        self.loaded_paths = loaded_paths

    @classmethod
    def load(cls, path: str | Path | None = None) -> ProjectConfig:
        config_path = (
            Path(path).expanduser().resolve()
            if path is not None
            else find_default_config()
        )
        if not config_path.is_file():
            raise ConfigurationError(f"configuration file not found: {config_path}")
        if config_path.suffix.casefold() != ".toml":
            raise ConfigurationError("HopWINS configuration must be a TOML file")

        data = _read_toml(config_path)
        loaded_paths = [config_path]
        local_path = config_path.with_name("config.local.toml")
        if local_path != config_path and local_path.is_file():
            _deep_merge(data, _read_toml(local_path))
            loaded_paths.append(local_path)
        if not isinstance(data.get("app"), dict):
            raise ConfigurationError("configuration requires an [app] table")
        return cls(config_path, data, tuple(loaded_paths))

    @property
    def task_name(self) -> str:
        value = str(self._table("app").get("task", "")).strip()
        if not value:
            raise ConfigurationError("[app] task must not be empty")
        return value

    @property
    def device_name(self) -> str | None:
        return _optional_text(self._table("app").get("device"))

    def device(self, name: str | None = None) -> DeviceConfig:
        device_name = name or self.device_name
        if not device_name:
            raise ConfigurationError("this task requires [app] device or --device")
        devices = self._table("devices")
        raw = devices.get(device_name)
        if not isinstance(raw, dict):
            raise ConfigurationError(f"missing [devices.{device_name}] table")

        port = str(raw.get("port", "")).strip()
        if not port:
            raise ConfigurationError(f"[devices.{device_name}] port must not be empty")
        baudrate = _as_int(raw.get("baudrate", 5_000_000), "baudrate")
        timeout_s = _as_float(raw.get("timeout_s", 0.1), "timeout_s")
        if baudrate <= 0:
            raise ConfigurationError("baudrate must be positive")
        if timeout_s <= 0:
            raise ConfigurationError("timeout_s must be positive")

        return DeviceConfig(
            name=device_name,
            port=port,
            baudrate=baudrate,
            timeout_s=timeout_s,
            vid=_optional_int(raw.get("vid"), "vid"),
            pid=_optional_int(raw.get("pid"), "pid"),
            serial_number=_optional_text(raw.get("serial_number")),
            description_contains=_optional_text(raw.get("description_contains")),
            expected_board=_optional_text(raw.get("expected_board")),
            expected_role=_optional_text(raw.get("expected_role")),
        )

    def task_parameters(
        self,
        task_name: str,
        overrides: dict[str, object] | None = None,
    ) -> dict[str, object]:
        tasks = self._table("tasks")
        raw = tasks.get(task_name, {})
        if not isinstance(raw, dict):
            raise ConfigurationError(f"[tasks.{task_name}] must be a table")
        parameters: dict[str, object] = copy.deepcopy(raw)
        if overrides:
            parameters.update(overrides)
        return parameters

    def task_text(
        self,
        task_name: str,
        option: str,
        *,
        fallback: str = "",
    ) -> str:
        value = self.task_parameters(task_name).get(option, fallback)
        return str(value).strip()

    def task_bool(
        self,
        task_name: str,
        option: str,
        *,
        fallback: bool,
    ) -> bool:
        value = self.task_parameters(task_name).get(option, fallback)
        if isinstance(value, bool):
            return value
        raise ConfigurationError(f"[tasks.{task_name}] {option} must be a boolean")

    def task_int(
        self,
        task_name: str,
        option: str,
        *,
        fallback: int,
    ) -> int:
        value = self.task_parameters(task_name).get(option, fallback)
        return _as_int(value, f"[tasks.{task_name}] {option}")

    def task_float(
        self,
        task_name: str,
        option: str,
        *,
        fallback: float,
    ) -> float:
        value = self.task_parameters(task_name).get(option, fallback)
        return _as_float(value, f"[tasks.{task_name}] {option}")

    def resolve_path(self, value: str | Path) -> Path:
        path = Path(value).expanduser()
        if path.is_absolute():
            return path
        return (self.path.parent / path).resolve()

    def experiment_directory(self) -> Path:
        storage = self._table("storage")
        directory = str(storage.get("experiment_directory", "experiments"))
        return self.resolve_path(directory)

    def new_session_path(
        self,
        task_name: str,
        device_name: str | None,
        label: str | None,
    ) -> Path:
        timestamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
        parts = [timestamp, _safe_name(task_name)]
        if device_name:
            parts.append(_safe_name(device_name))
        if label:
            parts.append(_safe_name(label))
        return self.experiment_directory() / "_".join(parts)

    def new_capture_path(self, task_name: str, device_name: str) -> Path:
        """Compatibility path for legacy single-file capture tasks."""
        storage = self._table("storage")
        directory = self.resolve_path(str(storage.get("capture_directory", "captures")))
        template = str(
            storage.get(
                "filename_template",
                "{timestamp}_{device}_{task}.hcir",
            )
        )
        values = {
            "timestamp": datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ"),
            "device": _safe_name(device_name),
            "task": _safe_name(task_name),
        }
        try:
            filename = template.format_map(values)
        except (KeyError, ValueError) as exc:
            raise ConfigurationError("invalid [storage] filename_template") from exc
        if not filename or Path(filename).name != filename:
            raise ConfigurationError("filename_template must produce one file name")
        return directory / filename

    def latest_capture_path(self) -> Path:
        storage = self._table("storage")
        capture_directory = self.resolve_path(
            str(storage.get("capture_directory", "captures"))
        )
        candidates = [
            path
            for path in capture_directory.glob("*.hcir")
            if path.stat().st_size > 0
        ]
        candidates.extend(
            path
            for path in self.experiment_directory().glob("*/raw/serial.bin")
            if path.stat().st_size > 0
        )
        if not candidates:
            raise ConfigurationError("no recorded serial streams found")
        return max(candidates, key=lambda path: path.stat().st_mtime_ns)

    def snapshot(self) -> dict[str, object]:
        return copy.deepcopy(self._data)

    def _table(self, name: str) -> dict[str, Any]:
        value = self._data.get(name, {})
        if not isinstance(value, dict):
            raise ConfigurationError(f"[{name}] must be a table")
        return value


def find_default_config() -> Path:
    environment_path = os.environ.get("HOPWINS_CONFIG")
    if environment_path:
        return Path(environment_path).expanduser().resolve()

    source_tree_path = Path(__file__).resolve().parents[2] / "config.toml"
    candidates = (
        Path.cwd() / "config.toml",
        Path.cwd() / "Python" / "config.toml",
        source_tree_path,
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return source_tree_path


def _read_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            data = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ConfigurationError(f"failed to read configuration: {path}") from exc
    return data


def _deep_merge(target: dict[str, Any], overlay: dict[str, Any]) -> None:
    for key, value in overlay.items():
        current = target.get(key)
        if isinstance(current, dict) and isinstance(value, dict):
            _deep_merge(current, value)
        else:
            target[key] = copy.deepcopy(value)


def _optional_text(value: object) -> str | None:
    if value is None:
        return None
    stripped = str(value).strip()
    return stripped or None


def _optional_int(value: object, name: str) -> int | None:
    if value is None or value == "":
        return None
    return _as_int(value, name)


def _as_int(value: object, name: str) -> int:
    if isinstance(value, bool):
        raise ConfigurationError(f"{name} must be an integer")
    if isinstance(value, int):
        return value
    try:
        return int(str(value), 0)
    except ValueError as exc:
        raise ConfigurationError(f"{name} must be an integer") from exc


def _as_float(value: object, name: str) -> float:
    if isinstance(value, bool):
        raise ConfigurationError(f"{name} must be a number")
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise ConfigurationError(f"{name} must be a number") from exc


def _safe_name(value: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9_-]+", "-", value.strip()).strip("-_")
    return safe or "session"
