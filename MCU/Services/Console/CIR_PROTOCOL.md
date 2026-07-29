# UWB CIR UART Protocol

USART1 carries startup/status text followed by self-synchronizing binary
packets. Every binary packet starts with `HCIR`, uses little-endian integer
fields, and ends with a CRC over the complete header and payload.

## Packet layout

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII magic `HCIR` |
| 4 | 1 | Protocol version, currently 1 |
| 5 | 1 | Type: 1 RX frame, 2 CIR data |
| 6 | 2 | Header length, currently 60 |
| 8 | 2 | Payload length in bytes |
| 10 | 2 | Flags: bit 0 RX CRC good, bit 1 ranging, bit 2 full 48-bit CIR |
| 12 | 4 | Capture ID |
| 16 | 2 | Chunk index |
| 18 | 2 | Chunk count |
| 20 | 8 | 40-bit DW3000 RX timestamp in the low bits |
| 28 | 4 | DW3000 system status |
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
| 60 | N | RX frame or CIR payload |
| 60+N | 4 | CRC32, little endian |

RSSI and first-path power use `0x8000` when the SDK cannot derive the value
from the captured diagnostics. This does not invalidate the raw CIR data.

The RX frame packet has one payload containing the received IEEE 802.15.4
frame. CIR packets contain up to 80 complex samples. `capture_id`,
`chunk_index`, and `chunk_count` let the host detect dropped packets without
depending on UART packet timing.

The CRC parameters match the STM32 CRC peripheral configuration:

```text
width=32 poly=0x04C11DB7 init=0xFFFFFFFF
refin=false refout=false xorout=0xFFFFFFFF
```

This is the CRC-32/BZIP2 parameter set. The CRC covers bytes `[0, 60+N)`;
the four CRC bytes themselves are excluded.

## Throughput

At 5 Mbps, 8-N-1 UART transports at most 500,000 payload bytes per second.
The default PRF64 Ipatov accumulator has 1016 samples. Full 48-bit I/Q uses
6 bytes per sample and 13 CIR packets. With a typical 23-byte received frame,
one capture occupies 7,015 UART bytes and takes about 14.03 ms on the wire.

| CIR samples | UART bytes per capture | Wire time | Ideal capture rate |
|---:|---:|---:|---:|
| 1016 | 7,015 | 14.03 ms | 71/s |
| 512 | 3,607 | 7.21 ms | 139/s |
| 256 | 1,879 | 3.76 ms | 266/s |
| 128 | 983 | 1.97 ms | 509/s |

These are UART-only upper bounds. SPI accumulator reads, packet processing,
host scheduling, and other console messages reduce the practical rate. At the
current one-frame-per-second test rate, exporting the complete CIR uses about
7 kB/s and is comfortably below the link limit.
