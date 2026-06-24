from __future__ import annotations

from bnt_core.wav import make_sine_wav


class FakeResponseService:
    def generate_response_audio(self, input_wav_bytes: bytes) -> bytes:
        return make_sine_wav(duration_ms=350, frequency_hz=660)
