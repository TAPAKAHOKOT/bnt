from __future__ import annotations

from dataclasses import dataclass, field
from enum import StrEnum
from typing import Protocol


class BntState(StrEnum):
    BOOT = "BOOT"
    IDLE = "IDLE"
    RECORDING = "RECORDING"
    SENDING = "SENDING"
    SPEAKING = "SPEAKING"
    ERROR = "ERROR"


class BntEvent(StrEnum):
    BOOT_COMPLETE = "BOOT_COMPLETE"
    BUTTON_PRESSED = "BUTTON_PRESSED"
    BUTTON_RELEASED = "BUTTON_RELEASED"
    MAX_RECORDING_REACHED = "MAX_RECORDING_REACHED"
    SEND_SUCCEEDED = "SEND_SUCCEEDED"
    SEND_FAILED = "SEND_FAILED"
    PLAYBACK_FINISHED = "PLAYBACK_FINISHED"
    PLAYBACK_FAILED = "PLAYBACK_FAILED"
    ERROR_HANDLED = "ERROR_HANDLED"
    TIMEOUT = "TIMEOUT"


class BntActions(Protocol):
    def start_recording(self) -> None: ...
    def stop_recording(self) -> None: ...
    def discard_recording(self) -> None: ...
    def start_send(self) -> None: ...
    def start_playback(self, audio: bytes | None = None) -> None: ...
    def stop_playback(self) -> None: ...
    def handle_error(self, code: str) -> None: ...
    def clear_error(self) -> None: ...
    def log_transition(self, previous: BntState, event: BntEvent, current: BntState) -> None: ...


class NoopActions:
    def start_recording(self) -> None:
        pass

    def stop_recording(self) -> None:
        pass

    def discard_recording(self) -> None:
        pass

    def start_send(self) -> None:
        pass

    def start_playback(self, audio: bytes | None = None) -> None:
        pass

    def stop_playback(self) -> None:
        pass

    def handle_error(self, code: str) -> None:
        pass

    def clear_error(self) -> None:
        pass

    def log_transition(self, previous: BntState, event: BntEvent, current: BntState) -> None:
        pass


@dataclass
class StateMachine:
    actions: BntActions = field(default_factory=NoopActions)
    min_recording_ms: int = 300
    current_state: BntState = BntState.BOOT
    last_error: str | None = None

    def handle(
        self,
        event: BntEvent,
        *,
        duration_ms: int | None = None,
        response_audio: bytes | None = None,
        error_code: str | None = None,
    ) -> BntState:
        previous = self.current_state

        if self.current_state == BntState.BOOT:
            self._handle_boot(event)
        elif self.current_state == BntState.IDLE:
            self._handle_idle(event)
        elif self.current_state == BntState.RECORDING:
            self._handle_recording(event, duration_ms)
        elif self.current_state == BntState.SENDING:
            self._handle_sending(event, response_audio, error_code)
        elif self.current_state == BntState.SPEAKING:
            self._handle_speaking(event, response_audio, error_code)
        elif self.current_state == BntState.ERROR:
            self._handle_error_state(event)

        if self.current_state != previous:
            self.actions.log_transition(previous, event, self.current_state)

        return self.current_state

    def _handle_boot(self, event: BntEvent) -> None:
        if event == BntEvent.BOOT_COMPLETE:
            self.current_state = BntState.IDLE

    def _handle_idle(self, event: BntEvent) -> None:
        if event == BntEvent.BUTTON_PRESSED:
            self.current_state = BntState.RECORDING
            self.actions.start_recording()

    def _handle_recording(self, event: BntEvent, duration_ms: int | None) -> None:
        if event == BntEvent.BUTTON_PRESSED:
            return

        if event == BntEvent.BUTTON_RELEASED:
            self.actions.stop_recording()
            if duration_ms is not None and duration_ms < self.min_recording_ms:
                self.current_state = BntState.IDLE
                self.actions.discard_recording()
                return
            self.current_state = BntState.SENDING
            self.actions.start_send()
            return

        if event == BntEvent.MAX_RECORDING_REACHED:
            self.actions.stop_recording()
            self.current_state = BntState.SENDING
            self.actions.start_send()

    def _handle_sending(
        self,
        event: BntEvent,
        response_audio: bytes | None,
        error_code: str | None,
    ) -> None:
        if event == BntEvent.SEND_SUCCEEDED:
            if not response_audio:
                self._enter_error("missing_response_audio")
                return
            self.current_state = BntState.SPEAKING
            self.actions.start_playback(response_audio)
            return

        if event in (BntEvent.SEND_FAILED, BntEvent.TIMEOUT):
            self._enter_error(error_code or event.value.lower())

    def _handle_speaking(
        self,
        event: BntEvent,
        response_audio: bytes | None,
        error_code: str | None,
    ) -> None:
        if event == BntEvent.PLAYBACK_FINISHED:
            self.current_state = BntState.IDLE
            return

        if event == BntEvent.BUTTON_PRESSED:
            self.actions.stop_playback()
            self.current_state = BntState.RECORDING
            self.actions.start_recording()
            return

        if event == BntEvent.PLAYBACK_FAILED:
            self._enter_error(error_code or "playback_failed")

    def _handle_error_state(self, event: BntEvent) -> None:
        if event == BntEvent.ERROR_HANDLED:
            self.actions.clear_error()
            self.last_error = None
            self.current_state = BntState.IDLE

    def _enter_error(self, code: str) -> None:
        self.last_error = code
        self.current_state = BntState.ERROR
        self.actions.handle_error(code)
