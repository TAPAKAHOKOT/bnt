from __future__ import annotations

import socket
import subprocess
import sys
import time
import urllib.request

import pytest

from bnt_core.state_machine import BntState
from bnt_core.wav import make_sine_wav
from simulator.bnt_simulator.app import run_fixture_flow
from simulator.bnt_simulator.backend_client import FakeBackendClient, HttpBackendClient


def test_fixture_flow_reaches_idle_and_plays_response() -> None:
    machine, actions, audio_output = run_fixture_flow(
        backend_client=FakeBackendClient(response_wav=make_sine_wav(duration_ms=250)),
        fixture_wav=make_sine_wav(duration_ms=400),
    )

    assert machine.current_state == BntState.IDLE
    assert len(audio_output.played_audio) == 1
    assert [transition[2] for transition in actions.transitions or []] == [
        BntState.IDLE,
        BntState.RECORDING,
        BntState.SENDING,
        BntState.SPEAKING,
        BntState.IDLE,
    ]


def test_fixture_flow_rejects_invalid_response_audio() -> None:
    machine, actions, audio_output = run_fixture_flow(
        backend_client=FakeBackendClient(response_wav=b"not wav"),
        fixture_wav=make_sine_wav(duration_ms=400),
    )

    assert machine.current_state == BntState.IDLE
    assert len(audio_output.played_audio) == 0
    assert BntState.ERROR in [transition[2] for transition in actions.transitions or []]


def test_fixture_flow_can_use_local_http_backend() -> None:
    port = _free_port()
    process = subprocess.Popen(
        [
            sys.executable,
            "-m",
            "uvicorn",
            "backend.app.main:app",
            "--host",
            "127.0.0.1",
            "--port",
            str(port),
            "--log-level",
            "warning",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    try:
        _wait_for_backend(port)
        machine, _actions, audio_output = run_fixture_flow(
            backend_client=HttpBackendClient(f"http://127.0.0.1:{port}", timeout_ms=2000),
            fixture_wav=make_sine_wav(duration_ms=400),
        )

        assert machine.current_state == BntState.IDLE
        assert len(audio_output.played_audio) == 1
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def _free_port() -> int:
    with socket.socket() as sock:
        try:
            sock.bind(("127.0.0.1", 0))
        except PermissionError:
            pytest.skip("local socket binding is not permitted in this environment")
        return int(sock.getsockname()[1])


def _wait_for_backend(port: int) -> None:
    deadline = time.monotonic() + 10
    url = f"http://127.0.0.1:{port}/health"

    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=0.5) as response:
                if response.status == 200:
                    return
        except OSError:
            time.sleep(0.1)

    raise RuntimeError("backend did not start")
