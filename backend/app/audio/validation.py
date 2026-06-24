from __future__ import annotations

from bnt_core.wav import WavInfo, validate_mvp_wav


def validate_request_wav(data: bytes) -> WavInfo:
    return validate_mvp_wav(data)
