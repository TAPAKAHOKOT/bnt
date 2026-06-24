from __future__ import annotations

import io
import math
import struct
import wave
from dataclasses import dataclass


MVP_SAMPLE_RATE = 16_000
MVP_CHANNELS = 1
MVP_BITS_PER_SAMPLE = 16
MVP_SAMPLE_WIDTH_BYTES = MVP_BITS_PER_SAMPLE // 8


@dataclass(frozen=True)
class WavInfo:
    sample_rate: int
    channels: int
    bits_per_sample: int
    frames: int
    duration_ms: int


class WavValidationError(ValueError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


def validate_mvp_wav(data: bytes) -> WavInfo:
    if not data:
        raise WavValidationError("empty_audio", "Audio payload is empty")

    try:
        with wave.open(io.BytesIO(data), "rb") as wav:
            channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            sample_rate = wav.getframerate()
            frames = wav.getnframes()
            bits_per_sample = sample_width * 8
    except (EOFError, wave.Error):
        raise WavValidationError("invalid_audio", "Audio payload is not a readable WAV file")

    if frames <= 0:
        raise WavValidationError("empty_audio", "Audio payload has no PCM frames")

    if channels != MVP_CHANNELS:
        raise WavValidationError("invalid_audio", "WAV must be mono")

    if sample_rate != MVP_SAMPLE_RATE:
        raise WavValidationError("invalid_audio", "WAV sample rate must be 16000 Hz")

    if bits_per_sample != MVP_BITS_PER_SAMPLE:
        raise WavValidationError("invalid_audio", "WAV must be 16-bit PCM")

    duration_ms = int(frames / sample_rate * 1000)
    return WavInfo(
        sample_rate=sample_rate,
        channels=channels,
        bits_per_sample=bits_per_sample,
        frames=frames,
        duration_ms=duration_ms,
    )


def make_sine_wav(duration_ms: int = 350, frequency_hz: int = 660, amplitude: float = 0.2) -> bytes:
    frame_count = max(1, int(MVP_SAMPLE_RATE * duration_ms / 1000))
    max_amplitude = int(32767 * amplitude)
    pcm = bytearray()

    for frame in range(frame_count):
        sample = int(max_amplitude * math.sin(2 * math.pi * frequency_hz * frame / MVP_SAMPLE_RATE))
        pcm.extend(struct.pack("<h", sample))

    return _write_wav(bytes(pcm), frame_count)


def make_silence_wav(duration_ms: int = 350) -> bytes:
    frame_count = max(1, int(MVP_SAMPLE_RATE * duration_ms / 1000))
    return _write_wav(b"\x00\x00" * frame_count, frame_count)


def _write_wav(pcm: bytes, frame_count: int) -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(MVP_CHANNELS)
        wav.setsampwidth(MVP_SAMPLE_WIDTH_BYTES)
        wav.setframerate(MVP_SAMPLE_RATE)
        wav.writeframes(pcm[: frame_count * MVP_SAMPLE_WIDTH_BYTES])
    return output.getvalue()
