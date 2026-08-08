"""Dataset task for the MCU ``DO-Follower`` workflow."""

from hopwins.core.prompts import PromptField, TaskPromptSpec
from hopwins.core.task import TaskContext
from hopwins.tasks.service_dataset import WorkflowDatasetProfile, run_workflow_dataset

PROFILE = WorkflowDatasetProfile(
    workflow="DO-Follower",
    record_rx_health=True,
    record_do_track=True,
    record_captures=True,
)
PROMPT = TaskPromptSpec(
    default_label="do-follower",
    default_notes="DO-Follower clock tracking, RX health, and CIR dataset.",
    fields=(
        PromptField(
            "duration_s",
            "Recording duration in seconds (0 = until Ctrl+C)",
            0.0,
            "float",
        ),
    ),
)


def run_configured(context: TaskContext) -> int:
    return run_workflow_dataset(context, PROFILE)
