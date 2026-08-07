"""Threaded compatibility workers used by the interactive CIR UI."""

from __future__ import annotations

import queue
import threading
import time
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

import serial

from hopwins.capture.assembler import CirCapture, CirCaptureAssembler
from hopwins.protocol.common_text import FirmwareProfile, parse_firmware_profile
from hopwins.protocol.packets import HcirPacket
from hopwins.protocol.stream_parser import HcirStreamParser, TextLine
from hopwins.storage.reader import resolve_raw_stream
from hopwins.storage.recorder import RawSessionRecorder


class RawSink(Protocol):
    def write(self, data: bytes) -> None: ...

    def set_profile(self, profile: FirmwareProfile) -> None: ...


@dataclass(frozen=True, slots=True)
class CaptureEvent:
    capture: CirCapture


@dataclass(frozen=True, slots=True)
class ProfileEvent:
    profile: FirmwareProfile


@dataclass(frozen=True, slots=True)
class TextEvent:
    line: str


@dataclass(frozen=True, slots=True)
class WorkerError:
    message: str


WorkerEvent = CaptureEvent | ProfileEvent | TextEvent | WorkerError


class EventWorker:
    def __init__(self, event_queue: queue.Queue[WorkerEvent]) -> None:
        self.events = event_queue
        self.parser = HcirStreamParser()
        self.assembler = CirCaptureAssembler()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self.last_error: str | None = None

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(target=self._run_guarded, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)

    @property
    def stopped(self) -> bool:
        return self._stop.is_set()

    def _run_guarded(self) -> None:
        try:
            self._run()
        except Exception as exc:
            self.last_error = str(exc)
            self._put_event(WorkerError(self.last_error))

    def _run(self) -> None:
        raise NotImplementedError

    def _process_bytes(
        self,
        data: bytes,
        recorder: RawSink | None = None,
    ) -> list[CirCapture]:
        captures: list[CirCapture] = []
        if recorder is not None:
            recorder.write(data)
        for event in self.parser.feed(data):
            if isinstance(event, TextLine):
                profile = parse_firmware_profile(event.text)
                if profile is not None:
                    self._accept_profile(profile)
                    if recorder is not None:
                        recorder.set_profile(profile)
                    self._put_event(ProfileEvent(profile))
                self._put_event(TextEvent(event.text))
                continue

            assert isinstance(event, HcirPacket)
            capture = self.assembler.add(event)
            if capture is not None:
                captures.append(capture)
                self._put_event(CaptureEvent(capture))
        return captures

    def _accept_profile(self, _profile: FirmwareProfile) -> None:
        pass

    def _put_event(self, event: WorkerEvent) -> None:
        try:
            self.events.put_nowait(event)
        except queue.Full:
            try:
                self.events.get_nowait()
            except queue.Empty:
                pass
            try:
                self.events.put_nowait(event)
            except queue.Full:
                pass


class SerialWorker(EventWorker):
    def __init__(
        self,
        port: str,
        baudrate: int,
        event_queue: queue.Queue[WorkerEvent],
        record_path: str | Path | None = None,
        *,
        timeout_s: float = 0.1,
        expected_board: str | None = None,
        expected_role: str | None = None,
        recorder_metadata: Mapping[str, object] | None = None,
        raw_sink: RawSink | None = None,
    ) -> None:
        super().__init__(event_queue)
        self.port = port
        self.baudrate = baudrate
        self.record_path = Path(record_path) if record_path else None
        self.timeout_s = timeout_s
        self.expected_board = expected_board
        self.expected_role = expected_role
        self.recorder_metadata = dict(recorder_metadata or {})
        self.raw_sink = raw_sink

    def _run(self) -> None:
        owns_recorder = self.raw_sink is None and self.record_path is not None
        recorder = self.raw_sink
        if recorder is None and self.record_path is not None:
            recorder = RawSessionRecorder(self.record_path, self.recorder_metadata)
        try:
            with serial.Serial(
                self.port,
                self.baudrate,
                timeout=self.timeout_s,
                write_timeout=0.5,
            ) as stream:
                while not self.stopped:
                    data = stream.read(65536)
                    if data:
                        self._process_bytes(data, recorder)
        finally:
            if owns_recorder and isinstance(recorder, RawSessionRecorder):
                recorder.close()

    def _accept_profile(self, profile: FirmwareProfile) -> None:
        mismatches = []
        if self.expected_board and profile.board != self.expected_board:
            mismatches.append(f"board={profile.board}, expected {self.expected_board}")
        if self.expected_role and profile.role != self.expected_role:
            mismatches.append(f"role={profile.role}, expected {self.expected_role}")
        if mismatches:
            raise RuntimeError(
                "connected firmware profile mismatch: " + "; ".join(mismatches)
            )


class ReplayWorker(EventWorker):
    def __init__(
        self,
        path: str | Path,
        event_queue: queue.Queue[WorkerEvent],
        speed: float = 1.0,
    ) -> None:
        super().__init__(event_queue)
        self.path = resolve_raw_stream(path)
        self.speed = max(speed, 0.01)

    def _run(self) -> None:
        previous_time: int | None = None
        with self.path.open("rb") as stream:
            while not self.stopped:
                data = stream.read(512)
                if not data:
                    break
                for capture in self._process_bytes(data):
                    current_time = capture.header.mcu_system_time_ms
                    if previous_time is not None and current_time:
                        delta_ms = (current_time - previous_time) & 0xFFFFFFFF
                        time.sleep(min(delta_ms / 1000.0 / self.speed, 0.25))
                    previous_time = current_time
