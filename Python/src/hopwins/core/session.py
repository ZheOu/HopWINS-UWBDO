"""Experiment-session lifecycle and reproducibility metadata."""

from __future__ import annotations

import json
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


class ExperimentSession:
    def __init__(self, path: Path, manifest: dict[str, Any]) -> None:
        self.path = path
        self.manifest = manifest
        self.path.mkdir(parents=True, exist_ok=False)
        (self.path / "raw").mkdir()
        (self.path / "records").mkdir()
        (self.path / "artifacts").mkdir()
        self._write_manifest()

    @classmethod
    def create(
        cls,
        path: Path,
        *,
        task: str,
        category: str,
        mode: str,
        protocol: str | None,
        device: str | None,
        parameters: dict[str, object],
        effective_config: dict[str, object],
        label: str | None,
        notes: str | None,
        source: str | None,
    ) -> ExperimentSession:
        now = datetime.now(UTC).isoformat()
        git_commit, git_dirty = _git_state(path)
        manifest: dict[str, Any] = {
            "session_id": path.name,
            "status": "running",
            "started_utc": now,
            "finished_utc": None,
            "task": task,
            "category": category,
            "mode": mode,
            "protocol": protocol,
            "device": device,
            "label": label,
            "notes": notes,
            "source": source,
            "parameters": parameters,
            "effective_config": effective_config,
            "git_commit": git_commit,
            "git_dirty": git_dirty,
            "statistics": {},
        }
        return cls(path, manifest)

    def update(self, **values: object) -> None:
        self.manifest.update(values)
        self._write_manifest()

    def finish(self, status: str, statistics: dict[str, object]) -> None:
        self.manifest["status"] = status
        self.manifest["finished_utc"] = datetime.now(UTC).isoformat()
        self.manifest["statistics"] = statistics
        self._write_manifest()

    def _write_manifest(self) -> None:
        destination = self.path / "manifest.json"
        temporary = destination.with_suffix(".json.tmp")
        temporary.write_text(
            json.dumps(self.manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary.replace(destination)


def _git_state(start: Path) -> tuple[str | None, bool | None]:
    working_directory = start
    while (
        not working_directory.exists()
        and working_directory != working_directory.parent
    ):
        working_directory = working_directory.parent
    try:
        commit_result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=working_directory,
            capture_output=True,
            check=False,
            text=True,
            timeout=2,
        )
        status_result = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=working_directory,
            capture_output=True,
            check=False,
            text=True,
            timeout=2,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None, None
    commit = commit_result.stdout.strip()
    if commit_result.returncode != 0 or not commit:
        return None, None
    dirty = status_result.returncode == 0 and bool(status_result.stdout.strip())
    return commit, dirty if status_result.returncode == 0 else None
