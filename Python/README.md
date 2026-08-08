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
  session lifecycle. `core/prompts.py` resolves reusable task input
  declarations without coupling task code to `input()` or `argparse`.
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

Dataset tasks also declare a default label, a default note, and selected
typed parameter prompts beside their own task code. Use `--prompt` to confirm
them before opening the serial port; press Enter to keep the value in brackets:

```powershell
hopwins run do_follower_dataset --device follower --prompt
# Dataset label [do-follower]: corridor-los-5m
# Dataset note [DO-Follower clock tracking, RX health, and CIR dataset.]: 5 m LOS
# Recording duration in seconds (0 = until Ctrl+C) [0]: 60
```

`--note` is a short alias for `--notes`. For unattended scripts, omit
`--prompt` and pass any values directly. Explicit command-line values have the
highest priority, task parameters from `config.toml` provide the next defaults,
and the task's `PROMPT` declaration is the final fallback. The resolved label,
note, and parameters are written to `manifest.json` in either mode.

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

## MCU workflow datasets

Each runnable workflow under `MCU/Services` has one Python dataset entry. The
task module owns only its tables; serial capture, host timestamps, offline
replay, manifests, and raw storage stay shared.

| MCU workflow | Python task | Structured outputs |
|---|---|---|
| `DO-Leader` | `do_leader_dataset` | `serial_text.csv`, `uwb_tx.csv` |
| `DO-Follower` | `do_follower_dataset` | `serial_text.csv`, `uwb_rx_health.csv`, `do_track.csv`, optional `captures.csv` |
| `UWB-STS-TX-Diagnostic` | `sts_tx_dataset` | `serial_text.csv`, `uwb_tx.csv` |
| `UWB-STS-Dual-RX-Diagnostic` | `dual_cir_dataset` | `uwb_rx_health.csv`, `dual_cir_pairs.csv`, paired I/Q binary |

Run a dataset directly from its matching board:

```powershell
hopwins run do_leader_dataset --device leader --duration 60 `
  --label leader-bench-01
hopwins run do_follower_dataset --device follower --duration 60 `
  --label follower-track-01
hopwins run sts_tx_dataset --device sts_tx --duration 60 `
  --label sts-tx-01
hopwins diagnose dual_cir_dataset --device sts_dual_rx --duration 60 `
  --label sts-dual-rx-01
```

The same commands can be run with `--prompt`; every dataset task has useful
label/note defaults, so quick captures no longer require repeated metadata
arguments. Keep using explicit `--label`, `--note`, `--duration`, and `--param`
values in automated runs.

Every online task writes lossless `raw/serial.bin` plus
`raw/serial.index.jsonl`. The index links every UART read to
`host_utc_ns` and `host_monotonic_ns`; structured rows repeat the relevant
host timestamps next to MCU/DW3000 timestamps. The recorder flushes raw,
index, event, and CSV streams periodically so a forced stop loses only a
small bounded tail.

Reorganize a saved Session without touching a serial port:

```powershell
hopwins run do_follower_dataset --mode offline --device follower `
  --input experiments/SESSION_NAME --label follower-reprocessed-v2
```

When multiple ST-Link boards are connected, put each board's serial number in
`config.local.toml` under `[devices.leader]`, `[devices.follower]`,
`[devices.sts_tx]`, or `[devices.sts_dual_rx]` so `port = "auto"` remains
unambiguous.

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

Create an algorithm-ready paired dataset directly from the RX board:

```powershell
hopwins diagnose dual_cir_dataset --device sts_dual_rx --duration 60 `
  --label los-3m-01 --notes "static boards, 3 m LOS"
```

The dataset Session keeps the lossless UART stream and also writes
`records/dual_cir_pairs.csv`, `artifacts/cir_iq_i32le.bin`, and
`artifacts/dataset.json`. Each CSV row is one UWB reception, links STS0 and
STS1 by capture ID, records both array offsets, FPI/RF/quality metadata, and
includes the experiment label. The binary file stores contiguous little-endian
`int32` rows in `[I, Q]` order without losing the original signed I24 values.

The same dataset can be derived later from an existing raw Session:

```powershell
hopwins diagnose dual_cir_dataset --mode offline `
  --input experiments/SESSION_NAME --label reprocessed-v1
```

Load one pair in an algorithm without parsing UART packets again:

```python
from hopwins.storage.dataset import DualCirDatasetReader

dataset = DualCirDatasetReader("experiments/SESSION_NAME")
pair = dataset.read_pair(0)
sts0_iq = pair.sts0  # shape: [samples, 2], columns: I and Q
sts1_iq = pair.sts1
```

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
│   ├── captures.csv
│   └── dual_cir_pairs.csv
└── artifacts/
    ├── dataset.json
    └── cir_iq_i32le.bin
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
5. If the task creates a dataset, add a small `PROMPT = TaskPromptSpec(...)`
   declaration for its default label, note, and user-facing typed parameters.
6. Add tests for the task's accepted record types and outputs.

For example, a new task can opt into the shared prompt behavior without adding
any CLI code:

```python
from hopwins.core.prompts import PromptField, TaskPromptSpec

PROMPT = TaskPromptSpec(
    default_label="new-task",
    default_notes="Default experiment note.",
    fields=(
        PromptField("duration_s", "Recording duration (s)", 0.0, "float"),
    ),
)
```

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
