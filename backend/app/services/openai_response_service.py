from __future__ import annotations

import io
import logging
import time
import uuid
import wave

import numpy as np
from openai import APITimeoutError, OpenAIError

from backend.app.config import BackendConfig
from backend.app.services.response_service import (
    ResponseServiceProviderError,
    ResponseServiceTimeout,
)
from bnt_core.wav import (
    MVP_SAMPLE_RATE,
    MVP_SAMPLE_WIDTH_BYTES,
    pcm16_to_mvp_wav,
)

logger = logging.getLogger("bnt.backend.openai")

SYSTEM_PROMPT = (
    "Ты — голосовой ассистент в маленькой носимой кнопке. Отвечай разговорным языком, "
    "без списков и markdown. Держи ответ примерно до 50 слов — максимум два коротких абзаца, "
    "если пользователь явно не попросил подробнее. Отвечай на языке пользователя."
)


class OpenAIResponseService:
    """Turn an input WAV into an MVP-format WAV reply via OpenAI STT -> chat -> TTS."""

    def __init__(self, config: BackendConfig, client: object | None = None) -> None:
        self._config = config
        self._client = client if client is not None else self._build_client(config)

    @staticmethod
    def _build_client(config: BackendConfig) -> object:
        # Imported lazily so the rest of the module stays importable without a key.
        from openai import OpenAI

        return OpenAI(
            api_key=config.openai_api_key,
            timeout=config.response_timeout_ms / 1000,
        )

    def generate_response_audio(self, input_wav_bytes: bytes) -> bytes:
        request_id = uuid.uuid4().hex[:8]
        started = time.monotonic()
        try:
            transcript = self._transcribe(input_wav_bytes)
            reply_text = self._chat(transcript)
            tts_audio = self._synthesize(reply_text)
        except APITimeoutError as exc:
            self._log(request_id, started, "timeout")
            raise ResponseServiceTimeout() from exc
        except OpenAIError as exc:
            self._log(request_id, started, "error")
            raise ResponseServiceProviderError() from exc

        wav_bytes = self._to_mvp_wav(tts_audio)
        self._log(request_id, started, "ok")
        return wav_bytes

    # --- OpenAI calls ---------------------------------------------------

    def _transcribe(self, input_wav_bytes: bytes) -> str:
        result = self._client.audio.transcriptions.create(
            model=self._config.openai_stt_model,
            file=("input.wav", io.BytesIO(input_wav_bytes), "audio/wav"),
        )
        return (getattr(result, "text", "") or "").strip()

    def _chat(self, transcript: str) -> str:
        completion = self._client.chat.completions.create(
            model=self._config.openai_chat_model,
            messages=[
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": transcript},
            ],
        )
        return (completion.choices[0].message.content or "").strip()

    def _synthesize(self, reply_text: str) -> bytes:
        response = self._client.audio.speech.create(
            model=self._config.openai_tts_model,
            voice=self._config.openai_tts_voice,
            input=reply_text,
            response_format="wav",
        )
        # The SDK returns a binary response wrapper exposing .read().
        if hasattr(response, "read"):
            return response.read()
        return getattr(response, "content", response)

    # --- transcoding ----------------------------------------------------

    def _to_mvp_wav(self, audio_bytes: bytes) -> bytes:
        samples, src_rate = self._decode_to_mono_float(audio_bytes)
        samples = self._resample(samples, src_rate, MVP_SAMPLE_RATE)
        pcm = self._float_to_pcm16(samples)
        pcm = self._cap_pcm(pcm)
        return pcm16_to_mvp_wav(pcm)

    @staticmethod
    def _decode_to_mono_float(audio_bytes: bytes) -> tuple[np.ndarray, int]:
        with wave.open(io.BytesIO(audio_bytes), "rb") as wav:
            channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            src_rate = wav.getframerate()
            frames = wav.readframes(wav.getnframes())

        if sample_width == 2:
            samples = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
        elif sample_width == 1:
            samples = (np.frombuffer(frames, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
        elif sample_width == 4:
            samples = np.frombuffer(frames, dtype="<i4").astype(np.float32) / 2147483648.0
        else:
            raise ResponseServiceProviderError(
                f"Unsupported TTS sample width: {sample_width} bytes"
            )

        if channels > 1:
            samples = samples.reshape(-1, channels).mean(axis=1)
        return samples, src_rate

    @staticmethod
    def _resample(samples: np.ndarray, src_rate: int, dst_rate: int) -> np.ndarray:
        if src_rate == dst_rate or samples.size == 0:
            return samples
        n_dst = int(round(samples.size * dst_rate / src_rate))
        if n_dst <= 0:
            return np.zeros(0, dtype=np.float32)
        src_positions = np.linspace(0.0, samples.size - 1, num=n_dst)
        return np.interp(src_positions, np.arange(samples.size), samples)

    @staticmethod
    def _float_to_pcm16(samples: np.ndarray) -> bytes:
        clipped = np.clip(samples, -1.0, 1.0)
        return (clipped * 32767.0).astype("<i2").tobytes()

    def _cap_pcm(self, pcm: bytes) -> bytes:
        max_frames_by_duration = int(
            self._config.max_response_duration_ms / 1000 * MVP_SAMPLE_RATE
        )
        # Leave headroom for the 44-byte WAV header within the byte cap.
        max_pcm_bytes = max(0, self._config.max_response_bytes - 44)
        max_frames_by_bytes = max_pcm_bytes // MVP_SAMPLE_WIDTH_BYTES
        max_frames = max(1, min(max_frames_by_duration, max_frames_by_bytes))
        return pcm[: max_frames * MVP_SAMPLE_WIDTH_BYTES]

    @staticmethod
    def _log(request_id: str, started: float, status: str) -> None:
        latency_ms = int((time.monotonic() - started) * 1000)
        logger.info("[openai] id=%s latency_ms=%s status=%s", request_id, latency_ms, status)
