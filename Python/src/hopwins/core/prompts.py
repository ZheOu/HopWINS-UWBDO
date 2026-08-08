"""Declarative, reusable inputs for experiment and dataset tasks."""

from __future__ import annotations

from collections.abc import Callable, Mapping
from dataclasses import dataclass
from typing import Literal

PromptKind = Literal["text", "int", "float", "bool"]
PromptValue = str | int | float | bool


@dataclass(frozen=True, slots=True)
class PromptField:
    """One task parameter that can be accepted from config, CLI, or a prompt."""

    key: str
    prompt: str
    default: PromptValue
    kind: PromptKind = "text"

    def __post_init__(self) -> None:
        if not self.key.strip():
            raise ValueError("prompt field key must not be empty")
        if not self.prompt.strip():
            raise ValueError("prompt text must not be empty")
        if self.kind not in {"text", "int", "float", "bool"}:
            raise ValueError(f"unsupported prompt kind: {self.kind}")


@dataclass(frozen=True, slots=True)
class TaskPromptSpec:
    """Human-facing metadata and parameter defaults owned by one task."""

    default_label: str
    default_notes: str = ""
    fields: tuple[PromptField, ...] = ()
    label_prompt: str = "Dataset label"
    notes_prompt: str = "Dataset note"

    def __post_init__(self) -> None:
        keys = [field.key for field in self.fields]
        if len(keys) != len(set(keys)):
            raise ValueError("task prompt field keys must be unique")


@dataclass(frozen=True, slots=True)
class ResolvedTaskInputs:
    """Values ready to be passed into :class:`TaskContext`."""

    label: str | None
    notes: str | None
    parameter_overrides: dict[str, object]


def resolve_task_inputs(
    spec: TaskPromptSpec,
    *,
    label: str | None,
    notes: str | None,
    effective_parameters: Mapping[str, object],
    parameter_overrides: Mapping[str, object],
    interactive: bool,
    input_fn: Callable[[str], str] | None = None,
) -> ResolvedTaskInputs:
    """Resolve task inputs with CLI > config > task-declared default precedence.

    Explicit CLI values are never prompted again. In interactive mode, pressing
    Enter accepts the displayed config or task default.
    """

    read = input if input_fn is None else input_fn
    resolved_label = _optional_text(label)
    resolved_notes = _optional_text(notes)

    if label is None:
        resolved_label = _prompt_text(
            spec.label_prompt,
            spec.default_label,
            interactive=interactive,
            input_fn=read,
        )
    if notes is None:
        resolved_notes = _prompt_text(
            spec.notes_prompt,
            spec.default_notes,
            interactive=interactive,
            input_fn=read,
        )

    overrides = dict(parameter_overrides)
    for field in spec.fields:
        if field.key in overrides:
            continue
        default = effective_parameters.get(field.key, field.default)
        if interactive:
            overrides[field.key] = _prompt_value(field, default, read)
        else:
            overrides[field.key] = _coerce_value(default, field.kind)

    return ResolvedTaskInputs(
        label=resolved_label,
        notes=resolved_notes,
        parameter_overrides=overrides,
    )


def _prompt_text(
    prompt: str,
    default: str,
    *,
    interactive: bool,
    input_fn: Callable[[str], str],
) -> str | None:
    if not interactive:
        return _optional_text(default)
    value = _read_input(prompt, default, input_fn)
    return _optional_text(value)


def _prompt_value(
    field: PromptField,
    default: object,
    input_fn: Callable[[str], str],
) -> PromptValue:
    while True:
        value = _read_input(field.prompt, _format_value(default), input_fn)
        try:
            return _coerce_value(value, field.kind)
        except ValueError as exc:
            print(f"Invalid value: {exc}. Please try again.")


def _read_input(
    prompt: str,
    default: str,
    input_fn: Callable[[str], str],
) -> str:
    suffix = f" [{default}]" if default else ""
    try:
        value = input_fn(f"{prompt}{suffix}: ")
    except EOFError as exc:
        raise ValueError(
            "interactive input is unavailable; remove --prompt or pass values "
            "on the command line"
        ) from exc
    stripped = value.strip()
    return stripped if stripped else default


def _coerce_value(value: object, kind: PromptKind) -> PromptValue:
    if kind == "text":
        return str(value)
    if kind == "bool":
        if isinstance(value, bool):
            return value
        folded = str(value).strip().casefold()
        if folded in {"1", "true", "yes", "y", "on"}:
            return True
        if folded in {"0", "false", "no", "n", "off"}:
            return False
        raise ValueError("expected true/false, yes/no, or 1/0")
    if isinstance(value, bool):
        raise ValueError(f"expected {kind}, not a boolean")
    if kind == "int":
        try:
            return int(str(value), 0)
        except ValueError as exc:
            raise ValueError("expected an integer") from exc
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError("expected a number") from exc


def _format_value(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def _optional_text(value: object) -> str | None:
    if value is None:
        return None
    text = str(value).strip()
    return text or None
