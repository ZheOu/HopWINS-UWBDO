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

Every name printed by `hopwins tasks` is directly executable and reads its
matching `[task.<name>]` section from `config.ini`:

```sh
hopwins tasks
hopwins list_ports
hopwins serial_probe --device follower
hopwins cir_monitor --device follower
hopwins raw_record --device follower
hopwins capture_inspect
hopwins static_timing_analysis
hopwins spatial_timing_analysis
hopwins replay
```

`hopwins run` executes the task selected by `[app]`. `--task` and `--device`
override `[app]` for one invocation:

```sh
hopwins run
hopwins run --task raw_record --device follower
```

The original argument-driven commands remain as compatibility shortcuts:

```sh
hopwins run cir_monitor --device follower
hopwins run capture_inspect --mode offline
hopwins run replay --mode offline
hopwins ports
hopwins monitor --port /dev/cu.usbserial-0001
hopwins record --port COM5 --output captures/session.hcir
hopwins replay captures/session.hcir
```

Use `serial_probe` before protocol debugging when the question is simply
whether bytes are arriving:

```sh
hopwins list_ports
hopwins serial_probe --device follower
```

It opens the configured port as 8-N-1 with flow control disabled, reports the
received byte rate, prints a hex/ASCII preview, and saves the exact stream to a
timestamped `.bin` file. It deliberately does not parse HCIR packets. Reset or
power-cycle the MCU after the port opens to include boot messages. A zero-byte
result exits with status 1 and points to a serial link, port ownership,
baudrate, wiring, or firmware-output problem rather than an HCIR parser issue.
Set `duration_s = 0` to run until Ctrl-C or `save = false` to suppress the raw
file.

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

`capture_inspect` prints a limited number of captures in human-readable form,
including RX status, adjusted `RX_STAMP`, FPI, peak indices, power diagnostics,
and a small I/Q table around the FPI. Configure `path`, `limit`, `cir_radius`,
and `frame_bytes` under `[task.capture_inspect]`.

The HCIR v2 `rx_timestamp` is the DW3000 fully adjusted RX timestamp: the CIA
first-path correction has been applied and `RXANTD` has been subtracted. New
firmware also writes coarse `RX_RAWST` into the formerly reserved final five
header bytes and sets `RAW_TIMESTAMP_VALID`; older captures remain readable
and report the raw value as unavailable. `capture_inspect` calculates
`signed40(RX_STAMP - RX_RAWST + RXANTD)` when both values are present.
HCIR v3 adds a CIR-source field plus Ipatov, STS0, and STS1 ToA/POA status,
TDoA, and PDoA. Two Mode-3 records with the same capture ID represent the two
STS sources; their `rf_port` fields identify the physical RF1/RF2 mapping. The
assembler keys pending data by `(capture_id, cir_source)`, so the pair remains
distinct while old HCIR v1/v2 recordings stay readable.

## STS dual-antenna diagnostic

Flash the TX workflow on the single-RF board and the dual-RX workflow on the
two-antenna board as described in `MCU/README.md`. Record the receiver's full
HCIR v3 stream with:

```sh
hopwins raw_record --device sts_rx
hopwins capture_inspect
```

The `sts_rx` device profile expects `Full-SiT5156` and
`UWB-STS-Dual-RX-Diagnostic`. `capture_inspect` prints the selected CIR source,
RF port, three CIA ToAs, STS status/phase values, TDoA, PDoA, and an I/Q window
around each source's own FPI. At the 100 ms TX interval, two complete
512-sample CIRs consume about 90.7 kB/s on the 5 Mbps UART link.

## Shared-clock static timing experiment

Record a stationary TX/RX pair with the same 38.4 MHz reference, then analyze
the resulting HCIR stream:

```sh
hopwins raw_record --device follower
# Stop after the desired sample count.
hopwins static_timing_analysis
```

The firmware schedules these test frames every 100 ms (10 Hz). A full
1016-sample capture is about 7,967 UART bytes, so this rate uses approximately
79.7 kB/s and 15.9% of the ideal 5 Mbps UART payload capacity.

The TX application payload already carries its 32-bit delayed-transmit time and
sequence number. The analysis therefore compares each 40-bit RX timestamp with
that frame's actual schedule instead of assuming that no packets were lost.
For accumulator index `n`, the plotted physical time is:

```text
t_cir(n) = RX_RAWST + n * 64 DTU + median(CIA-FPI) - RXANTD
t_plot(n) = t_cir(n) - (TX_SCHEDULE << 8) - median(arrival)
```

`median(CIA-FPI)` is one robust session calibration constant, not a per-frame
alignment. The CIR therefore remains tied to `RX_RAWST` and the TX schedule;
a false CIA first-path decision can move the RXTS histogram while a stable
physical CIR remains in place. Older captures without `RX_RAWST` use the
algebraically equivalent `RX_STAMP + n * 64 DTU - FPI_Q10.6` compatibility
path.
The `0 ns` origin is the median of all valid
`signed40(RX_STAMP - TX_SCHEDULE)` values in the selected session, not the
first frame. Fixed counter epoch, propagation, antenna, and PCB delays are
therefore removed without giving one noisy frame special status.

The main CIR trace is the noncoherent mean power `mean(I^2 + Q^2)`. Direct
complex averaging is not the default because packet-to-packet common phase can
rotate and cancel a stable channel. The plot also shows median power, a
10th-90th percentile band, the first frame, and a phase-aligned coherent
average for comparison.

The CIR-match timestamp treats the stable multipath response as one timing
fingerprint instead of assigning identities to individual peaks. The first
`correlation_template_frames` valid frames create a fixed template. Every
frame is searched only within `correlation_search_half_range_ns`, and the
normalized correlation peak is refined with a three-point parabola. The
reported timestamp is anchored to the session median:

```text
CIR_MATCH_TS = TX_SCHEDULE + median(arrival) + matched_profile_shift
```

`complex` mode is the default for a fully static, shared-clock experiment. It
uses the magnitude of complex normalized correlation, which rejects one common
I/Q phase rotation while preserving the phase structure across multipath taps.
`amplitude` mode subtracts a per-frame noise floor, compresses the envelope,
and is the safer fallback when small environmental changes alter relative path
phases. Correlation score and the margin to the strongest separated
alternative peak are exported for quality gating.

The original RXTS, `RAWST + FPI`, and CIR-match histograms use the same
nanosecond reference as the CIR. The histogram uses an independent visible
range: it defaults to `-10..+10 ns` and expands symmetrically when any valid
timing point falls outside that range. Original and CIR-match timestamps have
separate stacked histograms with the same viewport. The final plot compares
both estimates versus TX sequence.

`[task.static_timing_analysis]` selects the capture, optional frame limit,
RF port, histogram width in DTU, minimum histogram half-range, initial CIR view
range, correlation mode/template/window/search/resolution, CSV export, and
whether to open the interactive plot. A file containing more than one RF port
is rejected
unless `rf_port` explicitly selects one, because RF1 and RF2 are different
channels and must not share an average. The generated `.timing.csv` preserves
per-frame RXTS, raw timestamp, FPI-equivalent timing, CIR-match timestamp,
correlation quality, strongest CIR tap, RSSI, and CIR similarity.
Absolute propagation delay still includes fixed TX/RX antenna and PCB delays;
the experiment intentionally removes only their session median.

## Dual-CIR spatial landmark timing

The STS Mode-3 diagnostic produces two complete HCIR v3 records per received
frame. Their header records the actual physical RF port, so both `AUTO_1_2`
and `AUTO_2_1` mappings are supported. Analyze a capture from that receiver
with:

```sh
hopwins raw_record --device sts_rx
# Stop after the desired sample count.
hopwins spatial_timing_analysis
```

This task requires complete paired records with `RAW_TIMESTAMP_VALID` and the
scheduled `HWDO` TX payload. It reconstructs the two accumulator windows onto
a common `RAWST`-referenced time axis, then selects persistent local CIR
patches with high dual-antenna coherence and rank-one spatial structure. Each
patch independently estimates its shift relative to the fixed training
template; a weighted consensus produces the spatial-landmark timestamp only
when enough patches agree.

The interactive view compares exactly three centered estimates: native DW
`RXTS`, RF1 full-CIR cross-correlation, and dual-antenna spatial landmarks.
The FPI-equivalent timestamp remains in the CSV only as a CIA diagnostic. The
view also shows mean RF1 and RF2 CIR power with percentile bands, their sequence
traces, the number of landmarks that participated in each consensus, and every
landmark's residual from that consensus. Filled markers are accepted votes;
crosses identify a patch that disagreed with the selected timestamp. Both the
CIR and histogram horizontal spans are selected from the recorded signal and
timing data, so the same task can be used for LoS and delayed NLoS profiles
without editing view bounds.

`[task.spatial_timing_analysis]` controls the training length, patch geometry,
matching range and resolution, correlation feature mode, spatial-quality gate,
consensus tolerance, CSV export, and whether to open the visualisation. Its
clock model is fitted once from `RX_RAWST` and applied identically to all three
estimates. `clock_drift_mode = linear` removes the deterministic independent-
clock rate error and reports it in ppb; use `median` only for a shared-clock
experiment where retaining any residual frequency trend is desired. The
generated `.spatial-timing.csv` includes the three timestamps, centered errors,
clock model, landmark vote, consensus quality, and candidate/inlier counts. Its
adjacent `.spatial-timing.landmarks.csv` has one row per frame and landmark,
including the local shift, correlation score, spatial quality, coherence,
rank-one score, track reliability, and final inlier decision. The estimator is
an offline experiment: a missing consensus deliberately appears as an
unavailable spatial timestamp instead of an unqualified clock-control update.

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
