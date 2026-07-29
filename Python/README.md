# HopWINS Python tools

The host tools read the mixed text and binary USART1 stream, validate HCIR
CRC-32/BZIP2 packets, assemble CIR captures, record raw sessions, and display
live channel diagnostics.

## Environment

Use 64-bit Python 3.12 on Windows AMD64 and macOS ARM64. Virtual environments
are local artifacts and must not be copied between computers.

macOS:

```sh
cd Python
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -e ".[dev]" -c requirements.lock
```

Windows PowerShell:

```powershell
cd Python
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -e ".[dev]" -c requirements.lock
```

## Commands

The normal entry point reads `config.ini`:

```sh
hopwins tasks
hopwins run
hopwins run --task raw_record --device follower
hopwins run --task replay
```

`--task` and `--device` override `[app]` for one invocation. The original
direct commands remain useful for quick tests:

```sh
hopwins ports
hopwins monitor --port /dev/cu.usbserial-0001
hopwins record --port COM5 --output captures/session.hcir
hopwins replay captures/session.hcir
```

## Configuration

`config.ini` is the portable, version-controlled configuration:

- `[app]` selects the active task and named device.
- `[device.follower]` and `[device.leader]` hold serial settings and the
  expected MCU board/role identity.
- `[task.<name>]` contains parameters owned by one registered task.
- `[storage]` defines timestamped capture names.

An optional `config.local.ini` in the same directory is loaded after
`config.ini`. Use it for the current `[app]` selection, machine-specific COM
ports, or ST-Link serial numbers; it is ignored by Git.
`config.local.ini.example` shows the supported overlay shape.

With `port = auto`, the tool filters ports by VID, PID, optional description,
and optional probe serial number. If two matching boards are connected, set a
different `serial_number` for each named device or use explicit ports. This
keeps the same configuration model portable across Windows AMD64 and macOS
ARM64.

Live monitoring records by default. Each `.hcir` file has a `.hcir.json`
sidecar containing the effective configuration, resolved serial connection,
firmware profile, time range, and byte count. The binary `.hcir` stream remains
the source record for every session.

## VS Code

Open the repository root and select the interpreter inside `Python/.venv` once
on each computer. The Run and Debug panel provides:

- `Python: Configured Task` runs `[app] task` from the same `config.ini` used
  by the terminal.
- `Python: Task Override` prompts for any registered task name without editing
  the shared configuration.
- `Python: Tests` runs the Python test suite.

Use VS Code debugging while developing parsers, analysis, or UI code. Use
`hopwins run` in a terminal for normal captures and unattended sessions; both
paths execute the same task registry and configuration loader.

Each module under `src/hopwins/tasks` owns its task-specific configuration and
exports `run_configured(context)`. Adding a task requires one module, one
`TaskDefinition` entry in `tasks/registry.py`, and an optional matching
`[task.<name>]` section. VS Code launch configurations do not need to change.

`pyproject.toml` is the dependency source of truth. `requirements.lock` pins
the tested package versions while pip selects the matching Windows AMD64 or
macOS ARM64 wheel. Update both platforms together when refreshing the lock.
