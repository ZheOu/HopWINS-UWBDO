"""CRC-32/BZIP2 used by the STM32 CRC peripheral and HCIR protocol."""

from functools import lru_cache

POLYNOMIAL = 0x04C11DB7
INITIAL = 0xFFFFFFFF
XOR_OUT = 0xFFFFFFFF
MASK = 0xFFFFFFFF


@lru_cache(maxsize=1)
def _table() -> tuple[int, ...]:
    entries: list[int] = []
    for value in range(256):
        crc = value << 24
        for _ in range(8):
            crc = ((crc << 1) ^ POLYNOMIAL) if crc & 0x80000000 else crc << 1
            crc &= MASK
        entries.append(crc)
    return tuple(entries)


def crc32_bzip2(data: bytes | bytearray | memoryview) -> int:
    """Return the non-reflected CRC-32/BZIP2 checksum for *data*."""

    crc = INITIAL
    table = _table()
    for value in data:
        index = ((crc >> 24) ^ value) & 0xFF
        crc = ((crc << 8) ^ table[index]) & MASK
    return crc ^ XOR_OUT
