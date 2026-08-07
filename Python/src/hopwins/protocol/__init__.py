"""Protocol decoder factory and existing HCIR wire models."""

from hopwins.protocol.packets import CirSource, HcirPacket, PacketFlags, PacketType
from hopwins.protocol.stream_parser import HcirStreamParser, TextLine


def create_decoder(name: str):  # type: ignore[no-untyped-def]
    if name in {"auto", "hcir", "hcir_v2", "hcir_v3", "mixed"}:
        from hopwins.protocol.hcir import HcirDecoder

        return HcirDecoder()
    raise ValueError(f"unknown protocol decoder: {name}")


__all__ = [
    "CirSource",
    "HcirPacket",
    "HcirStreamParser",
    "PacketFlags",
    "PacketType",
    "TextLine",
    "create_decoder",
]
