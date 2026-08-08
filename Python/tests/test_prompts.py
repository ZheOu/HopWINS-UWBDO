from __future__ import annotations

import unittest
from pathlib import Path
from unittest.mock import patch

from hopwins.config import ProjectConfig
from hopwins.core.prompts import PromptField, TaskPromptSpec, resolve_task_inputs
from hopwins.registry import run_task, task_prompt_spec


class TaskPromptTests(unittest.TestCase):
    def setUp(self) -> None:
        self.spec = TaskPromptSpec(
            default_label="default-run",
            default_notes="default note",
            fields=(
                PromptField("duration_s", "Duration", 0.0, "float"),
                PromptField("keep_raw", "Keep raw data", True, "bool"),
            ),
        )

    def test_noninteractive_resolution_uses_config_then_task_defaults(self) -> None:
        resolved = resolve_task_inputs(
            self.spec,
            label=None,
            notes=None,
            effective_parameters={"duration_s": 15},
            parameter_overrides={"custom": "value"},
            interactive=False,
        )

        self.assertEqual(resolved.label, "default-run")
        self.assertEqual(resolved.notes, "default note")
        self.assertEqual(resolved.parameter_overrides["duration_s"], 15.0)
        self.assertIs(resolved.parameter_overrides["keep_raw"], True)
        self.assertEqual(resolved.parameter_overrides["custom"], "value")

    def test_explicit_cli_values_are_not_prompted_again(self) -> None:
        def fail_if_called(_: str) -> str:
            self.fail("input must not be called for explicit CLI values")

        resolved = resolve_task_inputs(
            self.spec,
            label="cli-label",
            notes="cli-note",
            effective_parameters={"duration_s": 60, "keep_raw": True},
            parameter_overrides={"duration_s": 5, "keep_raw": False},
            interactive=True,
            input_fn=fail_if_called,
        )

        self.assertEqual(resolved.label, "cli-label")
        self.assertEqual(resolved.notes, "cli-note")
        self.assertEqual(resolved.parameter_overrides["duration_s"], 5)
        self.assertIs(resolved.parameter_overrides["keep_raw"], False)

    def test_interactive_resolution_accepts_default_and_typed_values(self) -> None:
        answers = iter(("", "3 m corridor LOS", "12.5", "no"))
        prompts: list[str] = []

        def answer(prompt: str) -> str:
            prompts.append(prompt)
            return next(answers)

        resolved = resolve_task_inputs(
            self.spec,
            label=None,
            notes=None,
            effective_parameters={"duration_s": 0, "keep_raw": True},
            parameter_overrides={},
            interactive=True,
            input_fn=answer,
        )

        self.assertEqual(resolved.label, "default-run")
        self.assertEqual(resolved.notes, "3 m corridor LOS")
        self.assertEqual(resolved.parameter_overrides["duration_s"], 12.5)
        self.assertIs(resolved.parameter_overrides["keep_raw"], False)
        self.assertIn("[default-run]", prompts[0])
        self.assertIn("[0]", prompts[2])

    def test_dataset_tasks_keep_their_prompt_defaults_beside_task_code(self) -> None:
        expected = {
            "session_capture": "session-capture",
            "do_leader_dataset": "do-leader",
            "do_follower_dataset": "do-follower",
            "sts_tx_dataset": "sts-tx",
            "dual_cir_dataset": "sts-dual-rx",
        }

        for task_name, label in expected.items():
            with self.subTest(task=task_name):
                prompt = task_prompt_spec(task_name)
                self.assertIsNotNone(prompt)
                assert prompt is not None
                self.assertEqual(prompt.default_label, label)
                self.assertTrue(prompt.default_notes)
                self.assertIn("duration_s", {field.key for field in prompt.fields})

    def test_registry_applies_defaults_for_direct_python_callers(self) -> None:
        config = ProjectConfig(
            Path("config.toml"),
            {
                "app": {"task": "do_leader_dataset"},
                "tasks": {"do_leader_dataset": {"duration_s": 21}},
            },
            (),
        )
        captured = []

        def run(context):  # type: ignore[no-untyped-def]
            captured.append(context)
            return 0

        with patch("hopwins.tasks.do_leader_dataset.run_configured", run):
            self.assertEqual(
                run_task(config, task_name="do_leader_dataset", mode="offline"),
                0,
            )

        context = captured[0]
        self.assertEqual(context.label, "do-leader")
        self.assertTrue(context.notes)
        self.assertEqual(context.parameters["duration_s"], 21.0)


if __name__ == "__main__":
    unittest.main()
