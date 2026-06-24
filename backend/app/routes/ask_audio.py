from __future__ import annotations

import logging
import time
import uuid

from fastapi import APIRouter, Depends, Header, Request
from fastapi.responses import JSONResponse, Response

from backend.app.audio.validation import validate_request_wav
from backend.app.config import BackendConfig, load_config
from backend.app.services.fake_response_service import FakeResponseService
from backend.app.services.response_service import ResponseService, ResponseServiceError
from bnt_core.wav import WavValidationError, validate_mvp_wav

router = APIRouter()
logger = logging.getLogger("bnt.backend.ask_audio")


def get_response_service() -> ResponseService:
    return FakeResponseService()


@router.post("/ask-audio")
async def ask_audio(
    request: Request,
    content_type: str | None = Header(default=None),
    content_length: int | None = Header(default=None),
    config: BackendConfig = Depends(load_config),
    response_service: ResponseService = Depends(get_response_service),
) -> Response:
    request_id = str(uuid.uuid4())
    started = time.monotonic()

    if content_type is None or content_type.split(";")[0].strip().lower() != "audio/wav":
        return _error("invalid_audio", "Content-Type must be audio/wav", request_id, status_code=400)

    if content_length is not None and content_length > config.max_request_bytes:
        return _error("payload_too_large", "Audio payload is too large", request_id, status_code=413)

    body = await request.body()
    logger.info("[request] id=%s POST /ask-audio bytes=%s", request_id, len(body))

    if len(body) > config.max_request_bytes:
        return _error("payload_too_large", "Audio payload is too large", request_id, status_code=413)

    try:
        audio_info = validate_request_wav(body)
    except WavValidationError as exc:
        return _error(exc.code, exc.message, request_id, status_code=400)

    logger.info(
        "[audio] id=%s duration_ms=%s sample_rate=%s",
        request_id,
        audio_info.duration_ms,
        audio_info.sample_rate,
    )

    try:
        response_audio = response_service.generate_response_audio(body)
        response_info = validate_mvp_wav(response_audio)
    except ResponseServiceError as exc:
        return _error(exc.code, exc.message, request_id, status_code=exc.status_code)
    except Exception:
        logger.exception("[error] id=%s code=internal_error", request_id)
        return _error("internal_error", "Backend failed to generate audio response", request_id, status_code=500)

    if len(response_audio) > config.max_response_bytes:
        return _error("internal_error", "Generated response exceeded MVP response limit", request_id, status_code=500)

    if response_info.duration_ms > config.max_response_duration_ms:
        return _error("internal_error", "Generated response exceeded MVP duration limit", request_id, status_code=500)

    total_latency_ms = int((time.monotonic() - started) * 1000)
    logger.info("[response] id=%s bytes=%s total_latency_ms=%s", request_id, len(response_audio), total_latency_ms)

    return Response(
        content=response_audio,
        media_type="audio/wav",
        headers={
            "X-BNT-Text": "stub response",
            "X-BNT-Request-Id": request_id,
        },
    )


def _error(code: str, message: str, request_id: str, *, status_code: int) -> JSONResponse:
    logger.info("[error] id=%s code=%s message=%s", request_id, code, message)
    return JSONResponse(
        status_code=status_code,
        content={
            "error": {
                "code": code,
                "message": message,
                "request_id": request_id,
            }
        },
    )
