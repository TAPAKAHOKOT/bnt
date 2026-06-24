from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class SimulatorConfig:
    backend_url: str = "http://127.0.0.1:8000"
    min_recording_ms: int = 300
    max_recording_ms: int = 20_000
    server_timeout_ms: int = 15_000


def load_config() -> SimulatorConfig:
    return SimulatorConfig(
        backend_url=os.getenv("BNT_BACKEND_URL", "http://127.0.0.1:8000"),
        min_recording_ms=int(os.getenv("BNT_MIN_RECORDING_MS", "300")),
        max_recording_ms=int(os.getenv("BNT_MAX_RECORDING_MS", "20000")),
        server_timeout_ms=int(os.getenv("BNT_SERVER_TIMEOUT_MS", "15000")),
    )
