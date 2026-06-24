from __future__ import annotations

from fastapi import FastAPI

from backend.app.config import load_config
from backend.app.logging import configure_logging
from backend.app.routes.ask_audio import router as ask_audio_router


config = load_config()
configure_logging(config.log_level)

app = FastAPI(title="bnt backend", version="0.1.0")
app.include_router(ask_audio_router)


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}
