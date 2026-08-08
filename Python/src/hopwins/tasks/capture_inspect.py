"""Human-readable inspection of recorded RX metadata and CIR samples."""

from __future__ import annotations

import math
from pathlib import Path
from typing import TYPE_CHECKING

import numpy as np

from hopwins.analysis.cir import decode_i24_q24, magnitude_db
from hopwins.capture.assembler import CirCapture
from hopwins.capture.reader import CaptureFileReader
from hopwins.protocol.packets import CirSource, PacketFlags

if TYPE_CHECKING:
    from hopwins.core.task import TaskContext

DW_TIME_UNIT_SECONDS = 1.0 / (499.2e6 * 128.0)
DW_TIMESTAMP_MASK = (1 << 40) - 1
CIR_SAMPLE_DTU = 64
CIR_SAMPLE_PERIOD_NS = CIR_SAMPLE_DTU * DW_TIME_UNIT_SECONDS * 1e9


def run(
    path: str | Path,
    *,
    limit: int = 3,
    cir_radius: int = 4,
    frame_bytes: int = 32,
) -> int:
    if limit <= 0:
        raise ValueError("limit must be positive")
    if cir_radius < 0 or frame_bytes < 0:
        raise ValueError("cir_radius and frame_bytes must not be negative")

    reader = CaptureFileReader(path)
    inspected = 0
    for capture in reader:
        if inspected:
            print()
        print(
            format_capture(
                capture,
                cir_radius=cir_radius,
                frame_bytes=frame_bytes,
            )
        )
        inspected += 1
        if inspected >= limit:
            break

    parser = reader.parser.statistics
    assembler = reader.assembler.statistics
    print(
        "\nInspection summary: "
        f"captures={inspected}, packets={parser.packets}, "
        f"crc_errors={parser.crc_errors}, "
        f"framing_errors={parser.framing_errors}, "
        f"incomplete={assembler.incomplete_captures}, "
        f"invalid_chunks={assembler.invalid_chunks}"
    )
    if inspected == 0:
        raise ValueError(f"no complete HCIR captures found in {path}")
    return 0


def run_configured(context: TaskContext) -> int:
    configured_path = context.parameter_text("path", fallback="latest")
    selected_path = context.input_path
    if selected_path is None:
        selected_path = (
            context.config.latest_capture_path()
            if configured_path.casefold() == "latest"
            else context.config.resolve_path(configured_path)
        )
    path = resolve_raw_stream(selected_path)
    return run(
        path,
        limit=context.parameter_int("limit", fallback=3),
        cir_radius=context.parameter_int("cir_radius", fallback=4),
        frame_bytes=context.parameter_int("frame_bytes", fallback=32),
    )


def format_capture(
    capture: CirCapture,
    *,
    cir_radius: int,
    frame_bytes: int,
) -> str:
    header = capture.header
    rx_stamp_seconds = header.rx_timestamp * DW_TIME_UNIT_SECONDS
    antenna_delay_ns = header.rx_antenna_delay * DW_TIME_UNIT_SECONDS * 1e9
    corrected_digital_toa = (
        header.rx_timestamp + header.rx_antenna_delay
    ) & DW_TIMESTAMP_MASK
    fpi = header.first_path_index
    fpi_local = fpi - header.capture_sample_offset
    register_peak_delta = header.peak_index - fpi
    timestamp_details = [
        (
            f"    RX_STAMP + RXANTD = 0x{corrected_digital_toa:010X} "
            "(CIA-corrected digital TOA; not RX_RAWST)"
        )
    ]
    if header.flags & PacketFlags.RAW_TIMESTAMP_VALID:
        cia_correction = header.cia_correction_dtu
        relation_offset = header.cia_fpi_offset_dtu
        assert cia_correction is not None
        assert relation_offset is not None
        timestamp_details.extend(
            (
                (
                    f"    RX_RAWST coarse = "
                    f"0x{header.raw_rx_timestamp:010X} "
                    f"({header.raw_rx_timestamp} DTU)"
                ),
                (
                    f"    C_CIA=signed40(RX_STAMP-RX_RAWST+RXANTD) = "
                    f"{cia_correction} DTU "
                    f"({cia_correction * DW_TIME_UNIT_SECONDS * 1e9:.6f} ns)"
                ),
                (
                    f"    C_CIA-FPI_Q10.6 = {relation_offset} DTU "
                    f"({relation_offset * DW_TIME_UNIT_SECONDS * 1e9:.6f} ns)"
                ),
            )
        )
    else:
        timestamp_details.extend(
            (
                "    RX_RAWST = not captured by this HCIR v2 packet",
                ("    raw-to-adjusted CIA correction = unavailable for this packet"),
            )
        )
    if header.version >= 3 and header.pdoa_diagnostic_valid:
        timestamp_details.extend(
            (
                (
                    f"    IP_TOA=0x{header.ipatov_timestamp:010X}, "
                    f"STS0_TOA=0x{header.sts0_timestamp:010X}, "
                    f"STS1_TOA=0x{header.sts1_timestamp:010X}"
                ),
                (
                    f"    selected {header.cir_source.name} TOA="
                    f"0x{header.cir_timestamp:010X}, "
                    f"TDOA={header.tdoa_dtu} DTU, "
                    f"PDOA={header.pdoa_radians:.6f} rad"
                ),
            )
        )
    else:
        timestamp_details.append(
            "    IP_TOA / STS_TOA = unavailable before HCIR v3"
        )

    lines = [
        f"Capture {header.capture_id} / {header.cir_source.name}",
        (
            f"  packet: HCIR v{header.version}, flags={_format_flags(header.flags)}, "
            f"frame_len={header.frame_length}, RF_port={header.rf_port}, "
            f"CIR_group={header.cir_group_size}, "
            f"chunks={capture.received_chunks}/{capture.expected_chunks}"
        ),
        "  time:",
        (
            f"    RX_STAMP adjusted = 0x{header.rx_timestamp:010X} "
            f"({header.rx_timestamp} DTU, {rx_stamp_seconds:.9f} s modulo wrap)"
        ),
        (
            f"    RXANTD = 0x{header.rx_antenna_delay:04X} "
            f"({header.rx_antenna_delay} DTU, {antenna_delay_ns:.3f} ns)"
        ),
        *timestamp_details,
        (
            f"    MCU={header.mcu_system_time_ms} ms, "
            f"TIM2={header.reference_time_ms} ms "
            f"(source={header.reference_time_source}), "
            f"DW_SYS_HI=0x{header.dw_system_time_hi32:08X}"
        ),
        "  path:",
        (
            f"    FPI={fpi:.6f} accumulator samples "
            f"(Q10.6=0x{header.first_path_index_q10_6:04X}), "
            f"capture-local={fpi_local:.6f}"
        ),
        (
            f"    early_FPI={header.early_first_path_index:.6f}, "
            f"confidence_q0.4={header.early_first_path_confidence_q0_4}, "
            f"register_peak={header.peak_index}, "
            f"peak-FPI={register_peak_delta:.6f} samples "
            f"({register_peak_delta * CIR_SAMPLE_PERIOD_NS:.3f} ns)"
        ),
        (
            f"    capture_window=[{header.capture_sample_offset}, "
            f"{header.capture_sample_offset + header.capture_sample_count}), "
            f"sample_period={CIR_SAMPLE_PERIOD_NS:.6f} ns"
        ),
        "  diagnostics:",
        (
            f"    RSSI={_format_optional_dbm(header.rssi_dbm)}, "
            f"FP_power={_format_optional_dbm(header.first_path_power_dbm)}, "
            f"F1/F2/F3={header.first_path_amplitude_1}/"
            f"{header.first_path_amplitude_2}/"
            f"{header.first_path_amplitude_3}, "
            f"threshold={header.first_path_threshold}"
        ),
        (
            f"    SYS_STATUS=0x{header.system_status_high:08X}"
            f"{header.system_status_low:08X}, "
            f"RX_FINFO=0x{header.rx_finfo:08X}, "
            f"CIA_DIAG=0x{header.cia_diag_0:08X}/"
            f"0x{header.cia_diag_1:08X}, DGC={header.dgc_decision}"
        ),
        (
            f"    carrier_integrator={header.carrier_integrator}, "
            f"clock_offset={header.clock_offset}, "
            f"status(diag/cir/register)="
            f"{header.diagnostic_status}/{header.cir_status}/"
            f"{header.register_status}"
        ),
        (
            f"    STS_status={header.sts0_status:#06x}/{header.sts1_status:#06x}, "
            f"POA={header.sts0_phase:#06x}/{header.sts1_phase:#06x}, "
            f"PDOA_status={header.pdoa_diagnostic_status}"
            if header.cir_source in (CirSource.STS0, CirSource.STS1)
            else "    STS/PDoA diagnostics: not selected"
        ),
        f"  frame[0:{frame_bytes}] = {capture.frame[:frame_bytes].hex(' ')}",
    ]
    lines.extend(_format_cir_window(capture, cir_radius))
    return "\n".join(lines)


def _format_cir_window(
    capture: CirCapture,
    radius: int,
) -> list[str]:
    header = capture.header
    if not capture.has_cir:
        return ["  CIR: unavailable"]

    i_values, q_values = decode_i24_q24(capture.cir_bytes)
    magnitudes = np.hypot(
        i_values.astype(np.float64),
        q_values.astype(np.float64),
    )
    relative_db = magnitude_db(i_values, q_values)
    observed_peak_local = int(np.argmax(magnitudes))
    observed_peak = header.capture_sample_offset + observed_peak_local
    observed_peak_delta = observed_peak - header.first_path_index
    nearest_fpi = math.floor(header.first_path_index + 0.5)
    first_sample = max(
        header.capture_sample_offset,
        nearest_fpi - radius,
    )
    last_sample = min(
        header.capture_sample_offset + len(i_values) - 1,
        nearest_fpi + radius,
    )

    lines = [
        "  CIR:",
        (
            f"    observed_max={observed_peak}, "
            f"observed_max-FPI={observed_peak_delta:.6f} samples "
            f"({observed_peak_delta * CIR_SAMPLE_PERIOD_NS:.3f} ns), "
            f"|CIR|={magnitudes[observed_peak_local]:.3f}"
        ),
        "    mark  acc_idx  local  delta_from_FPI_ns          I          Q"
        "        |IQ|   dB_rel",
    ]
    if first_sample > last_sample:
        lines.append("    FPI is outside the exported CIR window")
        return lines

    for accumulator_index in range(first_sample, last_sample + 1):
        local_index = accumulator_index - header.capture_sample_offset
        marks = ""
        if accumulator_index == nearest_fpi:
            marks += "F"
        if accumulator_index == header.peak_index:
            marks += "R"
        if accumulator_index == observed_peak:
            marks += "M"
        delta_ns = (accumulator_index - header.first_path_index) * CIR_SAMPLE_PERIOD_NS
        lines.append(
            f"    {marks or '-':>4}  {accumulator_index:7d}  "
            f"{local_index:5d}  {delta_ns:17.6f}  "
            f"{int(i_values[local_index]):9d}  "
            f"{int(q_values[local_index]):9d}  "
            f"{magnitudes[local_index]:10.3f}  "
            f"{relative_db[local_index]:7.2f}"
        )
    lines.append("    marks: F=nearest FPI, R=register peak, M=observed max")
    return lines


def _format_flags(flags: PacketFlags) -> str:
    names = [flag.name for flag in PacketFlags if flags & flag]
    return "|".join(names) if names else "none"


def _format_optional_dbm(value: float | None) -> str:
    return "unavailable" if value is None else f"{value:.2f} dBm"
