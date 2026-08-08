"""Dataset task for the MCU ``UWB-STS-TX-Diagnostic`` workflow."""

from hopwins.core.prompts import PromptField, TaskPromptSpec
from hopwins.core.task import TaskContext
from hopwins.tasks.service_dataset import WorkflowDatasetProfile, run_workflow_dataset

PROFILE = WorkflowDatasetProfile(
    workflow="UWB-STS-TX-Diagnostic",
    record_tx=True,
)
PROMPT = TaskPromptSpec(
    default_label="sts-tx",
    default_notes="STS Mode 1 SDC transmitter diagnostic dataset.",
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
