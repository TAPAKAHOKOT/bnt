from __future__ import annotations

import os
from dataclasses import dataclass


DEFAULT_OPENAI_CHAT_MODEL = "gpt-4o-mini"
DEFAULT_OPENAI_STT_MODEL = "whisper-1"
DEFAULT_OPENAI_TTS_MODEL = "gpt-4o-mini-tts"
DEFAULT_OPENAI_TTS_VOICE = "sol"


@dataclass(frozen=True)
class BackendConfig:
    max_request_bytes: int = 716_800
    # Playback is streamed on the device, so response length is not RAM-bound.
    # These are generous safety ceilings (~30 s) — the concise system prompt keeps
    # real answers short. Bytes must cover the duration: 30 s * 16000 * 2 + header.
    max_response_bytes: int = 1_000_000
    max_response_duration_ms: int = 30_000
    audio_sample_rate: int = 16_000
    audio_channels: int = 1
    audio_bits_per_sample: int = 16
    log_level: str = "info"
    response_timeout_ms: int = 30_000
    openai_api_key: str | None = None
    openai_chat_model: str = DEFAULT_OPENAI_CHAT_MODEL
    openai_stt_model: str = DEFAULT_OPENAI_STT_MODEL
    openai_tts_model: str = DEFAULT_OPENAI_TTS_MODEL
    openai_tts_voice: str = DEFAULT_OPENAI_TTS_VOICE


def load_config() -> BackendConfig:
    api_key = os.getenv("OPENAI_API_KEY") or None
    return BackendConfig(
        max_request_bytes=int(os.getenv("BNT_MAX_REQUEST_BYTES", "716800")),
        max_response_bytes=int(os.getenv("BNT_MAX_RESPONSE_BYTES", "1000000")),
        max_response_duration_ms=int(os.getenv("BNT_MAX_RESPONSE_DURATION_MS", "30000")),
        audio_sample_rate=int(os.getenv("BNT_AUDIO_SAMPLE_RATE", "16000")),
        audio_channels=int(os.getenv("BNT_AUDIO_CHANNELS", "1")),
        audio_bits_per_sample=int(os.getenv("BNT_AUDIO_BITS_PER_SAMPLE", "16")),
        log_level=os.getenv("BNT_LOG_LEVEL", "info"),
        response_timeout_ms=int(os.getenv("BNT_RESPONSE_TIMEOUT_MS", "30000")),
        openai_api_key=api_key,
        openai_chat_model=os.getenv("OPENAI_CHAT_MODEL", DEFAULT_OPENAI_CHAT_MODEL),
        openai_stt_model=os.getenv("OPENAI_STT_MODEL", DEFAULT_OPENAI_STT_MODEL),
        openai_tts_model=os.getenv("OPENAI_TTS_MODEL", DEFAULT_OPENAI_TTS_MODEL),
        openai_tts_voice=os.getenv("OPENAI_TTS_VOICE", DEFAULT_OPENAI_TTS_VOICE),
    )
