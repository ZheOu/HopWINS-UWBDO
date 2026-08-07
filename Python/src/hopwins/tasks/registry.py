"""Compatibility imports for the root task registry."""

from hopwins.core.task import TaskContext, TaskSpec
from hopwins.registry import TASKS, run_task, task_definitions


def run_configured_task(*args, **kwargs):  # type: ignore[no-untyped-def]
    return run_task(*args, **kwargs)


TaskDefinition = TaskSpec

__all__ = [
    "TASKS",
    "TaskContext",
    "TaskDefinition",
    "run_configured_task",
    "task_definitions",
]
