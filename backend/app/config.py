from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class BackendConfig:
    max_request_bytes: int = 716_800
    max_response_bytes: int = 204_800
    max_response_duration_ms: int = 5_000
    audio_sample_rate: int = 16_000
    audio_channels: int = 1
    audio_bits_per_sample: int = 16
    log_level: str = "info"


def load_config() -> BackendConfig:
    return BackendConfig(
        max_request_bytes=int(os.getenv("BNT_MAX_REQUEST_BYTES", "716800")),
        max_response_bytes=int(os.getenv("BNT_MAX_RESPONSE_BYTES", "204800")),
        max_response_duration_ms=int(os.getenv("BNT_MAX_RESPONSE_DURATION_MS", "5000")),
        audio_sample_rate=int(os.getenv("BNT_AUDIO_SAMPLE_RATE", "16000")),
        audio_channels=int(os.getenv("BNT_AUDIO_CHANNELS", "1")),
        audio_bits_per_sample=int(os.getenv("BNT_AUDIO_BITS_PER_SAMPLE", "16")),
        log_level=os.getenv("BNT_LOG_LEVEL", "info"),
    )
