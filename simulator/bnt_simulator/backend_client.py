from __future__ import annotations

import urllib.error
import urllib.request
import socket
from dataclasses import dataclass

from bnt_core.wav import make_sine_wav


class BackendClientError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass
class FakeBackendClient:
    response_wav: bytes | None = None
    fail_code: str | None = None

    def ask_audio(self, input_wav: bytes) -> bytes:
        if self.fail_code:
            raise BackendClientError(self.fail_code, "fake backend failure")
        return self.response_wav or make_sine_wav(duration_ms=350, frequency_hz=660)


@dataclass
class HttpBackendClient:
    backend_url: str
    timeout_ms: int = 15_000

    def ask_audio(self, input_wav: bytes) -> bytes:
        request = urllib.request.Request(
            f"{self.backend_url.rstrip('/')}/ask-audio",
            data=input_wav,
            headers={"Content-Type": "audio/wav"},
            method="POST",
        )

        try:
            with urllib.request.urlopen(request, timeout=self.timeout_ms / 1000) as response:
                content_type = response.headers.get("Content-Type", "")
                if response.status != 200 or "audio/wav" not in content_type:
                    raise BackendClientError("send_failed", f"unexpected backend response {response.status}")
                return response.read()
        except urllib.error.HTTPError as exc:
            raise BackendClientError("send_failed", f"backend returned {exc.code}") from exc
        except urllib.error.URLError as exc:
            if isinstance(exc.reason, socket.timeout):
                raise BackendClientError("timeout", "backend request timed out") from exc
            raise BackendClientError("send_failed", str(exc)) from exc
        except socket.timeout as exc:
            raise BackendClientError("timeout", "backend request timed out") from exc
        except TimeoutError as exc:
            raise BackendClientError("timeout", "backend request timed out") from exc
        except OSError as exc:
            raise BackendClientError("send_failed", str(exc)) from exc
