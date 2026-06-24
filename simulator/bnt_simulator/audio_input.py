from __future__ import annotations

from dataclasses import dataclass

from bnt_core.wav import make_sine_wav


@dataclass
class FakeAudioInput:
    fixture_wav: bytes | None = None
    is_recording: bool = False

    def start_recording(self) -> None:
        self.is_recording = True

    def stop_recording(self) -> bytes:
        self.is_recording = False
        return self.fixture_wav or make_sine_wav(duration_ms=400, frequency_hz=440)
