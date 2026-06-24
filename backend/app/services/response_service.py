from __future__ import annotations

from typing import Protocol


class ResponseServiceError(RuntimeError):
    def __init__(self, code: str, message: str, status_code: int = 500) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.status_code = status_code


class ResponseServiceTimeout(ResponseServiceError):
    def __init__(self, message: str = "AI response timed out") -> None:
        super().__init__("openai_timeout", message, status_code=504)


class ResponseServiceProviderError(ResponseServiceError):
    def __init__(self, message: str = "AI provider returned an error") -> None:
        super().__init__("openai_error", message, status_code=502)


class ResponseService(Protocol):
    def generate_response_audio(self, input_wav_bytes: bytes) -> bytes: ...
