from __future__ import annotations

import io
import wave
from types import SimpleNamespace

import httpx
import numpy as np
import pytest
from openai import APITimeoutError, OpenAIError

from backend.app.config import BackendConfig
from backend.app.routes.ask_audio import get_response_service
from backend.app.services.fake_response_service import FakeResponseService
from backend.app.services.openai_response_service import (
    SYSTEM_PROMPT,
    OpenAIResponseService,
)
from backend.app.services.response_service import (
    ResponseServiceProviderError,
    ResponseServiceTimeout,
)
from bnt_core.wav import make_sine_wav, validate_mvp_wav


def _make_wav(rate: int, duration_ms: int, channels: int = 1) -> bytes:
    frames = int(rate * duration_ms / 1000)
    pcm = np.zeros(frames * channels, dtype="<i2").tobytes()
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(pcm)
    return buffer.getvalue()


class _FakeOpenAIClient:
    """In-memory stand-in for the OpenAI SDK client. No network access."""

    def __init__(
        self,
        *,
        transcript: str = "Привет",
        reply: str = "Привет! Чем помочь?",
        tts_wav: bytes | None = None,
        stt_exc: Exception | None = None,
        chat_exc: Exception | None = None,
        tts_exc: Exception | None = None,
    ) -> None:
        self.calls: dict[str, dict] = {}
        self._transcript = transcript
        self._reply = reply
        self._tts_wav = tts_wav if tts_wav is not None else _make_wav(24_000, 400)
        self._stt_exc = stt_exc
        self._chat_exc = chat_exc
        self._tts_exc = tts_exc

        self.audio = SimpleNamespace(
            transcriptions=SimpleNamespace(create=self._stt_create),
            speech=SimpleNamespace(create=self._tts_create),
        )
        self.chat = SimpleNamespace(
            completions=SimpleNamespace(create=self._chat_create)
        )

    def _stt_create(self, **kwargs):
        self.calls["stt"] = kwargs
        if self._stt_exc:
            raise self._stt_exc
        return SimpleNamespace(text=self._transcript)

    def _chat_create(self, **kwargs):
        self.calls["chat"] = kwargs
        if self._chat_exc:
            raise self._chat_exc
        message = SimpleNamespace(content=self._reply)
        return SimpleNamespace(choices=[SimpleNamespace(message=message)])

    def _tts_create(self, **kwargs):
        self.calls["tts"] = kwargs
        if self._tts_exc:
            raise self._tts_exc
        return SimpleNamespace(read=lambda: self._tts_wav)


def _config(**overrides) -> BackendConfig:
    base = dict(openai_api_key="sk-test")
    base.update(overrides)
    return BackendConfig(**base)


def test_pipeline_calls_stt_chat_tts_and_returns_mvp_wav() -> None:
    client = _FakeOpenAIClient(reply="Готово.")
    service = OpenAIResponseService(_config(), client=client)

    output = service.generate_response_audio(make_sine_wav())

    # All three OpenAI stages were exercised.
    assert {"stt", "chat", "tts"} <= set(client.calls)

    # System prompt is delivered as the first chat message.
    messages = client.calls["chat"]["messages"]
    assert messages[0] == {"role": "system", "content": SYSTEM_PROMPT}
    assert messages[1]["role"] == "user"
    assert messages[1]["content"] == "Привет"

    # The chat reply is what gets synthesized.
    assert client.calls["tts"]["input"] == "Готово."

    # Configured models flow through to the SDK calls.
    assert client.calls["stt"]["model"] == "whisper-1"
    assert client.calls["chat"]["model"] == "gpt-4o-mini"

    # Output is a valid MVP WAV.
    info = validate_mvp_wav(output)
    assert info.sample_rate == 16_000
    assert info.channels == 1
    assert info.bits_per_sample == 16
    assert info.frames > 0


def test_multi_turn_includes_prior_history_in_chat() -> None:
    from backend.app.services.conversation import ConversationMemory

    memory = ConversationMemory(ttl_seconds=300)
    service = OpenAIResponseService(_config(), client=_FakeOpenAIClient(reply="раз"), memory=memory)
    service.generate_response_audio(make_sine_wav())  # first turn

    second_client = _FakeOpenAIClient(transcript="а ещё?", reply="два")
    service2 = OpenAIResponseService(_config(), client=second_client, memory=memory)
    service2.generate_response_audio(make_sine_wav())

    messages = second_client.calls["chat"]["messages"]
    # system + prior (user/assistant) + new user
    assert messages[0]["role"] == "system"
    assert messages[1] == {"role": "user", "content": "Привет"}
    assert messages[2] == {"role": "assistant", "content": "раз"}
    assert messages[-1] == {"role": "user", "content": "а ещё?"}


def test_resamples_tts_audio_to_16k() -> None:
    client = _FakeOpenAIClient(tts_wav=_make_wav(24_000, 480))
    service = OpenAIResponseService(_config(), client=client)

    info = validate_mvp_wav(service.generate_response_audio(make_sine_wav()))

    assert info.sample_rate == 16_000
    # 480 ms in, ~480 ms out after resampling.
    assert abs(info.duration_ms - 480) <= 10


def test_downmixes_stereo_tts_to_mono() -> None:
    client = _FakeOpenAIClient(tts_wav=_make_wav(24_000, 300, channels=2))
    service = OpenAIResponseService(_config(), client=client)

    info = validate_mvp_wav(service.generate_response_audio(make_sine_wav()))

    assert info.channels == 1
    assert info.sample_rate == 16_000


def test_caps_response_by_duration() -> None:
    client = _FakeOpenAIClient(tts_wav=_make_wav(24_000, 5_000))
    service = OpenAIResponseService(
        _config(max_response_duration_ms=1_000, max_response_bytes=10_000_000),
        client=client,
    )

    info = validate_mvp_wav(service.generate_response_audio(make_sine_wav()))

    assert info.duration_ms <= 1_000


def test_caps_response_by_bytes() -> None:
    client = _FakeOpenAIClient(tts_wav=_make_wav(24_000, 5_000))
    service = OpenAIResponseService(
        _config(max_response_duration_ms=15_000, max_response_bytes=16_044),
        client=client,
    )

    output = service.generate_response_audio(make_sine_wav())

    assert len(output) <= 16_044


def test_timeout_maps_to_response_service_timeout() -> None:
    timeout = APITimeoutError(request=httpx.Request("POST", "https://api.openai.com/v1/audio"))
    client = _FakeOpenAIClient(stt_exc=timeout)
    service = OpenAIResponseService(_config(), client=client)

    with pytest.raises(ResponseServiceTimeout) as exc_info:
        service.generate_response_audio(make_sine_wav())

    assert exc_info.value.code == "openai_timeout"
    assert exc_info.value.status_code == 504


def test_provider_error_maps_to_provider_error() -> None:
    client = _FakeOpenAIClient(chat_exc=OpenAIError("boom"))
    service = OpenAIResponseService(_config(), client=client)

    with pytest.raises(ResponseServiceProviderError) as exc_info:
        service.generate_response_audio(make_sine_wav())

    assert exc_info.value.code == "openai_error"
    assert exc_info.value.status_code == 502


def test_get_response_service_uses_fake_without_key(monkeypatch) -> None:
    monkeypatch.delenv("BNT_RESPONSE_SERVICE", raising=False)
    service = get_response_service(BackendConfig(openai_api_key=None))
    assert isinstance(service, FakeResponseService)


def test_get_response_service_uses_openai_with_key(monkeypatch) -> None:
    monkeypatch.delenv("BNT_RESPONSE_SERVICE", raising=False)
    service = get_response_service(BackendConfig(openai_api_key="sk-test"))
    assert isinstance(service, OpenAIResponseService)


def test_get_response_service_forced_fake_ignores_key(monkeypatch) -> None:
    monkeypatch.setenv("BNT_RESPONSE_SERVICE", "fake")
    service = get_response_service(BackendConfig(openai_api_key="sk-test"))
    assert isinstance(service, FakeResponseService)


def test_get_response_service_forced_openai(monkeypatch) -> None:
    monkeypatch.setenv("BNT_RESPONSE_SERVICE", "openai")
    service = get_response_service(BackendConfig(openai_api_key="sk-test"))
    assert isinstance(service, OpenAIResponseService)
