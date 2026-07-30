# UWB RX and CIR UART Protocol

USART1 carries startup/status text followed by self-synchronizing binary
packets. Every binary packet starts with `HCIR`, uses little-endian integer
fields, and ends with a CRC over the complete header and payload.

Protocol v2 sends one type-1 RX frame packet for every CRC-correct UWB frame,
even when diagnostic or CIR reads fail. Type-2 packets carry CIR chunks when
the CIR-valid flag is set. The host must use `header length`, rather than a
hard-coded payload offset, so future protocol versions can extend the header.

## Packet layout

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII magic `HCIR` |
| 4 | 1 | Protocol version, currently 2 |
| 5 | 1 | Type: 1 RX frame, 2 CIR data |
| 6 | 2 | Header length, currently 128 |
| 8 | 2 | Payload length in bytes |
| 10 | 2 | Flags, described below |
| 12 | 4 | Capture ID |
| 16 | 2 | Chunk index |
| 18 | 2 | Chunk count |
| 20 | 8 | 40-bit fully adjusted DW3000 `RX_STAMP` in the low bits |
| 28 | 4 | `SYS_STATUS_LO` captured at RX completion |
| 32 | 4 | Signed carrier integrator |
| 36 | 2 | Signed clock offset |
| 38 | 2 | Received frame length |
| 40 | 2 | First-path index, Q10.6 |
| 42 | 2 | Peak index |
| 44 | 2 | Accumulated symbol count |
| 46 | 2 | First sample in the exported capture |
| 48 | 2 | First sample represented by this payload |
| 50 | 2 | CIR samples in this payload |
| 52 | 2 | CIR samples in the complete capture |
| 54 | 1 | Bytes per complex sample, currently 6 |
| 55 | 1 | CIR format: 1 means little-endian signed I24, Q24 |
| 56 | 2 | Signed RSSI in Q8.8 dBm |
| 58 | 2 | Signed first-path power in Q8.8 dBm |
| 60 | 1 | Forced DW3000 RF port, currently 1 |
| 61 | 1 | Reference time source: 0 unavailable, 1 external TIM2 in ms |
| 62 | 2 | RX antenna delay |
| 64 | 4 | MCU SysTick observation time in ms |
| 68 | 4 | External TIM2 reference time in ms |
| 72 | 4 | DW3000 system time high 32 bits at host readout |
| 76 | 4 | `SYS_STATUS_HI` |
| 80 | 4 | `RX_FINFO` |
| 84 | 4 | `CIA_DIAG_0` |
| 88 | 4 | `CIA_DIAG_1` |
| 92 | 4 | Ipatov CIR channel power |
| 96 | 4 | First-path amplitude F1 |
| 100 | 4 | First-path amplitude F2 |
| 104 | 4 | First-path amplitude F3 |
| 108 | 4 | Peak amplitude, Q20.2 |
| 112 | 4 | First-path threshold |
| 116 | 2 | Early first-path index, Q10.6 |
| 118 | 1 | Early first-path confidence, Q0.4 |
| 119 | 1 | DGC decision |
| 120 | 1 | Signed DW3000 diagnostic status |
| 121 | 1 | Signed DW3000 CIR-read status |
| 122 | 1 | Signed DW3000 register-snapshot status |
| 123 | 5 | 40-bit coarse `RX_RAWST`, valid when flag bit 7 is set |
| 128 | N | RX frame or CIR payload |
| 128+N | 4 | CRC32, little endian |

Status values are the signed `dw3000_status_t` codes stored in one byte. Zero
means success. RSSI and first-path power use `0x8000` when unavailable.

## Flags

| Bit | Mask | Meaning |
|---:|---:|---|
| 0 | `0x0001` | UWB frame CRC passed |
| 1 | `0x0002` | Ranging frame |
| 2 | `0x0004` | Full signed I24/Q24 CIR format |
| 3 | `0x0008` | CIA diagnostic fields are valid |
| 4 | `0x0010` | CIR samples are valid |
| 5 | `0x0020` | Register snapshot is valid |
| 6 | `0x0040` | External TIM2 timestamp is valid |
| 7 | `0x0080` | Coarse `RX_RAWST` is valid |

The RX frame packet has one payload containing the received IEEE 802.15.4
frame. CIR packets contain up to 80 complex samples. `capture_id`,
`chunk_index`, and `chunk_count` let the host assemble out-of-order chunks and
detect loss without depending on UART packet timing.

## Timestamp and FPI interpretation

The timestamp at offset 20 comes from `dwt_readrxtimestamp()`. It is the
DW3000 `RX_STAMP`, not `RX_RAWST`: the CIA leading-edge correction has already
been applied and the configured `RXANTD` has been subtracted. Adding the
reported antenna delay back removes only antenna-delay compensation; it does
not reconstruct the raw timestamp because the CIA correction remains.

The first-path index is Q10.6 in accumulator-sample units. At PRF64 one
accumulator sample corresponds to 64 DW3000 time units, approximately
1.001603 ns, and one FPI fractional unit corresponds to one DW3000 time unit,
approximately 15.65 ps. The FPI is relative to the complete accumulator;
subtract `capture sample offset` before indexing the exported CIR window.

The optional raw timestamp at offset 123 comes from
`dwt_readrxtimestampunadj()`. Its least significant byte is zero, so it has
coarse 8 ns resolution. For packets carrying both timestamps, define the
signed modular CIA correction in DW3000 time units as:

```text
C_CIA = signed40(RX_STAMP - RX_RAWST + RXANTD)
```

The FPI Q10.6 integer is also expressed in 1/64-sample increments, equal to
one DW3000 time unit at PRF64. The quantity `C_CIA - FPI_Q10_6` can therefore
be plotted across packets to test whether the remaining preamble/SFD alignment
term is constant for a fixed radio profile. Do not assume it is constant
without measurement because the manual describes additional CIA adjustments.

Separate `IP_TOA` and `STS_TOA` are not carried. With the current
`STS_MODE=OFF` profile, `RX_STAMP` is the Ipatov CIA result and is also written
to `IP_TOA`. A future STS-enabled profile should export both TOA values
explicitly.

## CRC and resynchronization

The CRC parameters match the STM32 CRC peripheral:

```text
width=32 poly=0x04C11DB7 init=0xFFFFFFFF
refin=false refout=false xorout=0xFFFFFFFF
```

This is CRC-32/BZIP2. The CRC covers bytes `[0, 128+N)`; the four CRC bytes are
excluded. A stream parser searches for `HCIR`, validates the two lengths, waits
for the complete packet, and accepts the boundary only after CRC validation.

## Throughput

At 5 Mbps, 8-N-1 UART transports at most 500,000 bytes per second. The default
PRF64 Ipatov accumulator has 1016 samples. Full 48-bit I/Q uses 6 bytes per
sample and 13 CIR packets. With a typical 23-byte received frame, one v2
capture occupies 7,967 UART bytes and takes about 15.93 ms on the wire.

| CIR samples | UART bytes per capture | Wire time | Ideal capture rate |
|---:|---:|---:|---:|
| 1016 | 7,967 | 15.93 ms | 62/s |
| 512 | 4,151 | 8.30 ms | 120/s |
| 256 | 2,219 | 4.44 ms | 225/s |
| 128 | 1,187 | 2.37 ms | 421/s |

These are UART-only upper bounds. SPI accumulator reads, host scheduling,
rendering, and other console messages reduce the practical rate. The host
recorder writes raw bytes before plotting so display refresh does not determine
data retention.
