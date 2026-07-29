"""Layered project configuration for host tasks and connected devices."""

from __future__ import annotations

import configparser
import os
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path


class ConfigurationError(ValueError):
    """Raised when a project configuration is missing or invalid."""


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
    """Effective configuration loaded from a shared file and local overlay."""

    def __init__(
        self,
        path: Path,
        parser: configparser.ConfigParser,
        loaded_paths: tuple[Path, ...],
    ) -> None:
        self.path = path
        self._parser = parser
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

        parser = configparser.ConfigParser(interpolation=None)
        loaded_paths = [config_path]
        local_path = config_path.parent / "config.local.ini"
        if local_path != config_path and local_path.is_file():
            loaded_paths.append(local_path)
        loaded = parser.read(loaded_paths, encoding="utf-8")
        if len(loaded) != len(loaded_paths):
            raise ConfigurationError("failed to read all configuration files")
        if not parser.has_section("app"):
            raise ConfigurationError("configuration requires an [app] section")
        return cls(config_path, parser, tuple(loaded_paths))

    @property
    def task_name(self) -> str:
        value = self._parser.get("app", "task", fallback="").strip()
        if not value:
            raise ConfigurationError("[app] task must not be empty")
        return value

    @property
    def device_name(self) -> str | None:
        return _optional_text(self._parser.get("app", "device", fallback=""))

    def device(self, name: str | None = None) -> DeviceConfig:
        device_name = name or self.device_name
        if not device_name:
            raise ConfigurationError("this task requires [app] device or --device")
        section = f"device.{device_name}"
        if not self._parser.has_section(section):
            raise ConfigurationError(f"missing [{section}] section")

        port = self._parser.get(section, "port", fallback="").strip()
        if not port:
            raise ConfigurationError(f"[{section}] port must not be empty")
        baudrate = self._get_int(section, "baudrate", fallback=5_000_000)
        timeout_s = self._get_float(section, "timeout_s", fallback=0.1)
        if baudrate <= 0:
            raise ConfigurationError(f"[{section}] baudrate must be positive")
        if timeout_s <= 0:
            raise ConfigurationError(f"[{section}] timeout_s must be positive")

        return DeviceConfig(
            name=device_name,
            port=port,
            baudrate=baudrate,
            timeout_s=timeout_s,
            vid=self._get_optional_int(section, "vid"),
            pid=self._get_optional_int(section, "pid"),
            serial_number=self._get_optional_text(section, "serial_number"),
            description_contains=self._get_optional_text(
                section, "description_contains"
            ),
            expected_board=self._get_optional_text(section, "expected_board"),
            expected_role=self._get_optional_text(section, "expected_role"),
        )

    def task_text(
        self,
        task_name: str,
        option: str,
        *,
        fallback: str = "",
    ) -> str:
        return self._parser.get(f"task.{task_name}", option, fallback=fallback).strip()

    def task_bool(
        self,
        task_name: str,
        option: str,
        *,
        fallback: bool,
    ) -> bool:
        try:
            return self._parser.getboolean(
                f"task.{task_name}", option, fallback=fallback
            )
        except ValueError as exc:
            raise ConfigurationError(
                f"[task.{task_name}] {option} must be a boolean"
            ) from exc

    def task_int(
        self,
        task_name: str,
        option: str,
        *,
        fallback: int,
    ) -> int:
        return self._get_int(f"task.{task_name}", option, fallback=fallback)

    def task_float(
        self,
        task_name: str,
        option: str,
        *,
        fallback: float,
    ) -> float:
        return self._get_float(f"task.{task_name}", option, fallback=fallback)

    def resolve_path(self, value: str) -> Path:
        path = Path(value).expanduser()
        if path.is_absolute():
            return path
        return (self.path.parent / path).resolve()

    def new_capture_path(self, task_name: str, device_name: str) -> Path:
        directory = self.resolve_path(
            self._parser.get(
                "storage",
                "capture_directory",
                fallback="captures",
            )
        )
        template = self._parser.get(
            "storage",
            "filename_template",
            fallback="{timestamp}_{device}_{task}.hcir",
        )
        values = {
            "timestamp": datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ"),
            "device": device_name,
            "task": task_name,
        }
        try:
            filename = template.format_map(values)
        except (KeyError, ValueError) as exc:
            raise ConfigurationError(
                "[storage] filename_template may use only "
                "{timestamp}, {device}, and {task}"
            ) from exc
        if not filename or Path(filename).name != filename:
            raise ConfigurationError(
                "[storage] filename_template must produce a file name"
            )
        return directory / filename

    def latest_capture_path(self) -> Path:
        directory = self.resolve_path(
            self._parser.get(
                "storage",
                "capture_directory",
                fallback="captures",
            )
        )
        captures = list(directory.glob("*.hcir"))
        if not captures:
            raise ConfigurationError(f"no .hcir captures found in {directory}")
        return max(captures, key=lambda path: path.stat().st_mtime_ns)

    def snapshot(self) -> dict[str, dict[str, str]]:
        return {
            section: dict(self._parser.items(section))
            for section in self._parser.sections()
        }

    def _get_optional_text(self, section: str, option: str) -> str | None:
        return _optional_text(self._parser.get(section, option, fallback=""))

    def _get_optional_int(self, section: str, option: str) -> int | None:
        value = self._parser.get(section, option, fallback="").strip()
        if not value:
            return None
        try:
            return int(value, 0)
        except ValueError as exc:
            raise ConfigurationError(
                f"[{section}] {option} must be an integer"
            ) from exc

    def _get_int(self, section: str, option: str, *, fallback: int) -> int:
        value = self._parser.get(section, option, fallback=str(fallback))
        try:
            return int(value, 0)
        except ValueError as exc:
            raise ConfigurationError(
                f"[{section}] {option} must be an integer"
            ) from exc

    def _get_float(
        self,
        section: str,
        option: str,
        *,
        fallback: float,
    ) -> float:
        value = self._parser.get(section, option, fallback=str(fallback))
        try:
            return float(value)
        except ValueError as exc:
            raise ConfigurationError(f"[{section}] {option} must be a number") from exc


def find_default_config() -> Path:
    environment_path = os.environ.get("HOPWINS_CONFIG")
    if environment_path:
        return Path(environment_path).expanduser().resolve()

    source_tree_path = Path(__file__).resolve().parents[2] / "config.ini"
    candidates = (
        Path.cwd() / "config.ini",
        Path.cwd() / "Python" / "config.ini",
        source_tree_path,
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return source_tree_path


def _optional_text(value: str) -> str | None:
    stripped = value.strip()
    return stripped or None
