from __future__ import annotations

import logging

from fastapi import FastAPI

from backend.app.config import load_config
from backend.app.logging import configure_logging
from backend.app.routes.ask_audio import router as ask_audio_router


config = load_config()
configure_logging(config.log_level)

# Surface the effective response caps at startup — these are the values that
# truncate/limit the reply, and they are easy to get wrong via a stale exported
# env var that overrides .env. Logging them removes the guesswork.
logging.getLogger("bnt.backend").info(
    "[boot] max_response_bytes=%s max_response_duration_ms=%s response_timeout_ms=%s "
    "conversation_ttl_ms=%s tts_voice=%s",
    config.max_response_bytes,
    config.max_response_duration_ms,
    config.response_timeout_ms,
    config.conversation_ttl_ms,
    config.openai_tts_voice,
)

app = FastAPI(title="bnt backend", version="0.1.0")
app.include_router(ask_audio_router)


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}
