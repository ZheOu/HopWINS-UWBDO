from hopwins.capture.assembler import CirCaptureAssembler
from hopwins.protocol.packets import CirSource, HcirPacket, PacketType
from hopwins.protocol.stream_parser import HcirStreamParser
from tests.helpers import build_v3_packet


def _parse(raw: bytes) -> HcirPacket:
    events = HcirStreamParser().feed(raw)
    return next(event for event in events if isinstance(event, HcirPacket))


def test_v3_decodes_dual_sts_metadata() -> None:
    packet = _parse(
        build_v3_packet(
            PacketType.RX_FRAME,
            cir_source=CirSource.STS1,
            rf_port=2,
            payload=b"frame",
        )
    )

    assert packet.header.version == 3
    assert packet.header.cir_source is CirSource.STS1
    assert packet.header.rf_port == 2
    assert packet.header.cir_group_size == 2
    assert packet.header.cir_timestamp == 0x3031323334
    assert packet.header.pdoa_q1_11 == -1024
    assert packet.header.pdoa_radians == -0.5
    assert packet.header.tdoa_dtu == -37


def test_assembler_keeps_two_sources_with_same_capture_id() -> None:
    assembler = CirCaptureAssembler()
    completed = []

    for source, port in ((CirSource.STS0, 1), (CirSource.STS1, 2)):
        frame = _parse(
            build_v3_packet(
                PacketType.RX_FRAME,
                capture_id=11,
                cir_source=source,
                rf_port=port,
                payload=b"frame",
                capture_sample_count=1,
            )
        )
        samples = _parse(
            build_v3_packet(
                PacketType.CIR_DATA,
                capture_id=11,
                cir_source=source,
                rf_port=port,
                payload=b"\x01\x02\x03\x04\x05\x06",
                payload_sample_count=1,
                capture_sample_count=1,
            )
        )
        assert assembler.add(frame) is None
        completed.append(assembler.add(samples))

    assert [capture.header.cir_source for capture in completed] == [
        CirSource.STS0,
        CirSource.STS1,
    ]
    assert [capture.header.rf_port for capture in completed] == [1, 2]
