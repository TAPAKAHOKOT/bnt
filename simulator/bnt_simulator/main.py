from __future__ import annotations

import argparse

from simulator.bnt_simulator.app import run_fixture_flow
from simulator.bnt_simulator.backend_client import FakeBackendClient, HttpBackendClient
from simulator.bnt_simulator.config import load_config
from simulator.bnt_simulator.logger import configure_logging
from bnt_core.state_machine import BntState


def main() -> int:
    configure_logging()
    config = load_config()

    parser = argparse.ArgumentParser(description="Run the bnt desktop simulator fixture flow.")
    parser.add_argument("--fake-backend", action="store_true", help="Use an in-process fake backend response.")
    parser.add_argument("--backend-url", default=config.backend_url, help="Backend base URL for HTTP fixture mode.")
    args = parser.parse_args()

    backend_client = FakeBackendClient() if args.fake_backend else HttpBackendClient(args.backend_url, config.server_timeout_ms)
    machine, actions, audio_output = run_fixture_flow(
        backend_client=backend_client,
        min_recording_ms=config.min_recording_ms,
    )

    print(f"final_state={machine.current_state}")
    print(f"transitions={len(actions.transitions or [])}")
    print(f"played_responses={len(audio_output.played_audio)}")
    return 0 if machine.current_state == BntState.IDLE else 1


if __name__ == "__main__":
    raise SystemExit(main())
