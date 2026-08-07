# HopWINS Python tools

The Python project records reproducible experiments and runs the same protocol
and task code against a live serial stream or a saved session. It keeps raw
bytes independent from decoding and processing so a newer parser or algorithm
can always replay an older experiment.

## Architecture

```text
main.py -> registry -> task
                       |
Source -> Decoder -> Record -> Task -> SessionRecorder
 serial   protocol             |
 replay                        +-> CSV / JSONL / artifacts
```

Responsibilities are intentionally narrow:

- `main.py` parses the command and dispatches a registered task.
- `registry.py` is the explicit list of tasks and their categories/modes.
- `core/` owns transport-neutral records, the pipeline, task contracts, and
  session lifecycle.
- `io/` owns online serial and offline replay sources. Interactive UI workers
  live here as compatibility adapters.
- `protocol/` converts byte chunks into typed records. Protocol code performs
  no file I/O and contains no experiment policy.
- `storage/` preserves raw chunks, their host timestamps, events, tables, and
  the manifest.
- `tasks/` owns experiment/diagnostic behavior but not serial framing.
- `analysis/` contains reusable numerical helpers, while `ui/` only displays.

Simple tasks are one module. Promote a task to a package only after it becomes
too large for one cohesive file.

## Environment

Use 64-bit Python 3.12 on Windows AMD64 or macOS ARM64.

Windows PowerShell:

```powershell
cd Python
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -e ".[dev]" -c requirements.lock
```

macOS:

```sh
cd Python
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -e ".[dev]" -c requirements.lock
```

## Configuration

Portable settings live in `config.toml`. Copy
`config.local.toml.example` to `config.local.toml` for the current computer's
COM port, ST-Link serial number, default task, or temporary duration. The local
file is deeply merged over the shared file and is ignored by Git.

The updated MCU board names are used by default:

- Follower: `UWB-RF1-SiT5156`, role `DO-Follower`
- Leader: `UWB-RF1`, role `DO-Leader`

Every session stores the fully merged configuration, all command-line
parameter overrides, the Git commit, and whether the working tree was dirty in
`manifest.json`.

## Commands

List the explicit registry:

```sh
hopwins tasks
hopwins tasks --category capture
```

Record a live session until Ctrl+C:

```sh
hopwins run session_capture --device follower --label cable-run-01
```

Record for a fixed duration and attach experiment notes:

```sh
hopwins run session_capture --device follower --duration 600 \
  --label corridor-los-5m --notes "static, RF1"
```

Replay an existing session through the same decoder and task, without copying
the raw stream into the derived session:

```sh
hopwins run session_capture --mode offline \
  --input experiments/SESSION_NAME --label reprocess-v2
```

Task parameters can be overridden without editing shared configuration:

```sh
hopwins run session_capture --param protocol=hcir \
  --param replay_speed=0
```

Existing utilities remain registered while they are migrated:

```sh
hopwins run cir_monitor --device follower
hopwins run capture_inspect --mode offline
hopwins run replay --mode offline
hopwins ports
```

## STS dual-channel CIR monitor

MCU commit `5058310` adds HCIR v3 for the
`UWB-STS-Dual-RX-Diagnostic` workflow. One received UWB frame produces two
complete records with the same capture ID: STS0 on RF1 and STS1 on RF2. Each
record has its own CIR chunks and repeats the PDoA, TDoA, and STS ToA metadata.

Connect Python to the diagnostic RX board and start live recording plus the
dynamic monitor:

```powershell
hopwins diagnose dual_cir_monitor --device sts_dual_rx `
  --label sts-bench-01 --notes "fixed TX/RX geometry"
```

The window shows both magnitudes with a shared dB reference, aligned to each
channel's FPI, plus separate raw I/Q plots. Closing the window finalizes an
experiment Session containing lossless `raw/serial.bin`, per-read timestamps,
the effective configuration, firmware profile, and Git state. Disable recording
only for a temporary display with `--param record=false`.

Replay a recorded Session through the same pairing and visualization code:

```powershell
hopwins diagnose dual_cir_monitor --mode offline `
  --input experiments/SESSION_NAME --param replay_speed=1
```

Generate tabular v3 metadata for later algorithms without opening the UI:

```powershell
hopwins run session_capture --mode offline `
  --input experiments/SESSION_NAME --label metadata-pass
```

The resulting `captures.csv` has one row per channel and includes capture ID,
CIR source, RF port, STS0/STS1 ToA, PDoA, TDoA, FPI, RSSI, and capture-window
metadata. The complete I/Q arrays remain losslessly available in the raw HCIR
stream so later algorithms can choose their own storage and preprocessing.

## Session layout

```text
experiments/TIMESTAMP_TASK_DEVICE_LABEL/
├── manifest.json
├── raw/
│   ├── serial.bin
│   └── serial.index.jsonl
├── records/
│   ├── events.jsonl
│   ├── do_track.csv
│   └── captures.csv
└── artifacts/
```

`serial.bin` is written before protocol decoding. `serial.index.jsonl` records
the offset, length, host monotonic timestamp, UTC timestamp, and source for
every read. This permits timing-aware offline replay and preserves malformed or
unknown packets for future decoders.

`do_track.csv` appears only when the firmware emits `DO TRACK` records.
`captures.csv` appears only when complete HCIR captures are assembled. Large
CIR I/Q arrays remain in the raw stream; future task modules may write NPZ or
other files under `artifacts/` without changing the recorder.

## Adding a protocol

1. Add one decoder module under `protocol/`.
2. Implement `feed(ByteChunk)`, `flush()`, and `statistics()`.
3. Return typed `Record` instances.
4. Register the decoder in `protocol.create_decoder()`.
5. Add fragmented, corrupt, and version-compatibility tests.

Shared framing belongs in protocol code. A task must never decode byte offsets
or calculate a packet CRC itself.

## Adding a task or diagnosis

1. Add one module under `tasks/`; diagnostics may use `tasks/diagnostics/`.
2. Implement `run_configured(context)` or use the shared `RecordTask` pipeline.
3. Add one `TaskSpec` entry to `registry.py`.
4. Add `[tasks.<name>]` defaults to `config.toml`.
5. Add tests for the task's accepted record types and outputs.

Diagnostics are ordinary tasks with category `diagnostic`; `main.py`, sources,
protocols, and storage do not change when a new diagnosis is added.

## Existing HCIR support

HCIR v2/v3 parsing, CRC-32/BZIP2 validation, source-aware CIR chunk assembly,
raw replay, CIR inspection, and the Qt monitors are supported. The mixed decoder also types
the new `FW PROFILE`, `DO TRACK CFG`, and `DO TRACK` text records. A Follower
using `SERIAL_CAPTURE_FORMAT_OFF` therefore produces DO tables without CIR;
`SERIAL_CAPTURE_FORMAT_HCIR_V2` produces both DO and capture tables.

## Tests

```sh
cd Python
python -m pytest
python -m ruff check src tests
```

Tests mirror the architecture: protocol parsing, storage/session behavior,
task outputs, and existing CIR/UI behavior are verified separately.
