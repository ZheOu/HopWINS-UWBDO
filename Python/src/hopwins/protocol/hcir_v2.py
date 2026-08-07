"""Compatibility import for the version-neutral HCIR decoder."""

from hopwins.protocol.hcir import HcirDecoder

HcirV2Decoder = HcirDecoder

__all__ = ["HcirV2Decoder"]
