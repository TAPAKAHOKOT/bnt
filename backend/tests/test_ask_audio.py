from __future__ import annotations

from fastapi.testclient import TestClient

from backend.app.config import BackendConfig, load_config
from backend.app.main import app
from backend.app.routes.ask_audio import get_response_service
from backend.app.services.response_service import ResponseServiceProviderError, ResponseServiceTimeout
from bnt_core.wav import make_sine_wav, validate_mvp_wav


client = TestClient(app)


def teardown_function() -> None:
    app.dependency_overrides.clear()


def test_ask_audio_accepts_valid_wav_and_returns_mvp_wav() -> None:
    response = client.post("/ask-audio", content=make_sine_wav(), headers={"Content-Type": "audio/wav"})

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("audio/wav")
    assert response.headers["x-bnt-request-id"]
    info = validate_mvp_wav(response.content)
    assert info.frames > 0


def test_ask_audio_accepts_raw_pcm_stream() -> None:
    # Raw 16-bit mono 16kHz PCM (no WAV header), as the firmware streams it.
    pcm = b"\x00\x01" * 1600  # 100 ms of PCM
    response = client.post("/ask-audio", content=pcm, headers={"Content-Type": "audio/L16;rate=16000;channels=1"})

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("audio/wav")
    info = validate_mvp_wav(response.content)
    assert info.frames > 0


def test_ask_audio_rejects_empty_raw_pcm() -> None:
    response = client.post("/ask-audio", content=b"", headers={"Content-Type": "audio/L16"})

    assert response.status_code == 400
    assert response.json()["error"]["code"] == "empty_audio"


def test_ask_audio_rejects_empty_audio() -> None:
    response = client.post("/ask-audio", content=b"", headers={"Content-Type": "audio/wav"})

    assert response.status_code == 400
    assert response.json()["error"]["code"] == "empty_audio"


def test_ask_audio_rejects_invalid_audio() -> None:
    response = client.post("/ask-audio", content=b"not wav", headers={"Content-Type": "audio/wav"})

    assert response.status_code == 400
    assert response.json()["error"]["code"] == "invalid_audio"


def test_ask_audio_rejects_wrong_content_type() -> None:
    response = client.post("/ask-audio", content=make_sine_wav(), headers={"Content-Type": "application/octet-stream"})

    assert response.status_code == 400
    assert response.json()["error"]["code"] == "invalid_audio"


def test_ask_audio_rejects_oversized_audio() -> None:
    app.dependency_overrides[load_config] = lambda: BackendConfig(max_request_bytes=10)

    response = client.post("/ask-audio", content=make_sine_wav(), headers={"Content-Type": "audio/wav"})

    assert response.status_code == 413
    assert response.json()["error"]["code"] == "payload_too_large"


def test_ask_audio_rejects_response_over_duration_limit() -> None:
    class LongResponseService:
        def generate_response_audio(self, input_wav_bytes: bytes) -> bytes:
            return make_sine_wav(duration_ms=5100)

    app.dependency_overrides[load_config] = lambda: BackendConfig(max_response_duration_ms=5000)
    app.dependency_overrides[get_response_service] = lambda: LongResponseService()

    response = client.post("/ask-audio", content=make_sine_wav(), headers={"Content-Type": "audio/wav"})

    assert response.status_code == 500
    assert response.json()["error"]["code"] == "internal_error"


def test_ask_audio_maps_response_service_timeout() -> None:
    class TimeoutResponseService:
        def generate_response_audio(self, input_wav_bytes: bytes) -> bytes:
            raise ResponseServiceTimeout()

    app.dependency_overrides[get_response_service] = lambda: TimeoutResponseService()

    response = client.post("/ask-audio", content=make_sine_wav(), headers={"Content-Type": "audio/wav"})

    assert response.status_code == 504
    assert response.json()["error"]["code"] == "openai_timeout"


def test_ask_audio_maps_response_service_provider_error() -> None:
    class ErrorResponseService:
        def generate_response_audio(self, input_wav_bytes: bytes) -> bytes:
            raise ResponseServiceProviderError()

    app.dependency_overrides[get_response_service] = lambda: ErrorResponseService()

    response = client.post("/ask-audio", content=make_sine_wav(), headers={"Content-Type": "audio/wav"})

    assert response.status_code == 502
    assert response.json()["error"]["code"] == "openai_error"
