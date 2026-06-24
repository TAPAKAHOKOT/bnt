from __future__ import annotations

import logging
from dataclasses import dataclass

from bnt_core.state_machine import BntEvent, BntState, StateMachine
from bnt_core.wav import WavValidationError, validate_mvp_wav

from simulator.bnt_simulator.audio_input import FakeAudioInput
from simulator.bnt_simulator.audio_output import FakeAudioOutput
from simulator.bnt_simulator.backend_client import BackendClientError, FakeBackendClient, HttpBackendClient

logger = logging.getLogger("bnt.simulator")


@dataclass
class SimulatorActions:
    audio_input: FakeAudioInput
    audio_output: FakeAudioOutput
    recorded_audio: bytes | None = None
    playback_failed: bool = False
    transitions: list[tuple[BntState, BntEvent, BntState]] | None = None

    def __post_init__(self) -> None:
        if self.transitions is None:
            self.transitions = []

    def start_recording(self) -> None:
        logger.info("[audio_in] start")
        self.audio_input.start_recording()

    def stop_recording(self) -> None:
        self.recorded_audio = self.audio_input.stop_recording()
        logger.info("[audio_in] stop bytes=%s", len(self.recorded_audio))

    def discard_recording(self) -> None:
        logger.info("[audio_in] discard")
        self.recorded_audio = None

    def start_send(self) -> None:
        logger.info("[network] send ready")

    def start_playback(self, audio: bytes | None = None) -> None:
        if audio is None:
            self.handle_error("missing_response_audio")
            self.playback_failed = True
            return
        logger.info("[audio_out] play bytes=%s", len(audio))
        try:
            self.audio_output.play(audio)
        except RuntimeError:
            self.playback_failed = True
            raise

    def stop_playback(self) -> None:
        logger.info("[audio_out] stop")
        self.audio_output.stop()

    def handle_error(self, code: str) -> None:
        logger.info("[error] code=%s", code)

    def clear_error(self) -> None:
        logger.info("[error] cleared")

    def log_transition(self, previous: BntState, event: BntEvent, current: BntState) -> None:
        assert self.transitions is not None
        self.transitions.append((previous, event, current))
        logger.info("[state] %s --%s--> %s", previous, event, current)


def run_fixture_flow(
    *,
    backend_client: FakeBackendClient | HttpBackendClient,
    fixture_wav: bytes | None = None,
    recording_duration_ms: int = 400,
    min_recording_ms: int = 300,
) -> tuple[StateMachine, SimulatorActions, FakeAudioOutput]:
    audio_input = FakeAudioInput(fixture_wav=fixture_wav)
    audio_output = FakeAudioOutput()
    actions = SimulatorActions(audio_input=audio_input, audio_output=audio_output)
    machine = StateMachine(actions=actions, min_recording_ms=min_recording_ms)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_PRESSED)
    machine.handle(BntEvent.BUTTON_RELEASED, duration_ms=recording_duration_ms)

    if machine.current_state == BntState.SENDING and actions.recorded_audio is not None:
        try:
            validate_mvp_wav(actions.recorded_audio)
            response_audio = backend_client.ask_audio(actions.recorded_audio)
            validate_mvp_wav(response_audio)
        except BackendClientError as exc:
            event = BntEvent.TIMEOUT if exc.code == "timeout" else BntEvent.SEND_FAILED
            machine.handle(event, error_code=exc.code)
            machine.handle(BntEvent.ERROR_HANDLED)
        except WavValidationError as exc:
            machine.handle(BntEvent.SEND_FAILED, error_code=exc.code)
            machine.handle(BntEvent.ERROR_HANDLED)
        except RuntimeError:
            machine.handle(BntEvent.PLAYBACK_FAILED, error_code="playback_failed")
            machine.handle(BntEvent.ERROR_HANDLED)
        else:
            try:
                machine.handle(BntEvent.SEND_SUCCEEDED, response_audio=response_audio)
            except RuntimeError:
                machine.handle(BntEvent.PLAYBACK_FAILED, error_code="playback_failed")
                machine.handle(BntEvent.ERROR_HANDLED)
            else:
                if actions.playback_failed:
                    machine.handle(BntEvent.PLAYBACK_FAILED, error_code="playback_failed")
                    machine.handle(BntEvent.ERROR_HANDLED)
                else:
                    machine.handle(BntEvent.PLAYBACK_FINISHED)

    return machine, actions, audio_output
