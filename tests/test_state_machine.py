from __future__ import annotations

from dataclasses import dataclass, field

from bnt_core.state_machine import BntEvent, BntState, StateMachine


@dataclass
class FakeActions:
    calls: list[str] = field(default_factory=list)
    transitions: list[tuple[BntState, BntEvent, BntState]] = field(default_factory=list)

    def start_recording(self) -> None:
        self.calls.append("start_recording")

    def stop_recording(self) -> None:
        self.calls.append("stop_recording")

    def discard_recording(self) -> None:
        self.calls.append("discard_recording")

    def start_send(self) -> None:
        self.calls.append("start_send")

    def start_playback(self, audio: bytes | None = None) -> None:
        self.calls.append(f"start_playback:{len(audio or b'')}")

    def stop_playback(self) -> None:
        self.calls.append("stop_playback")

    def handle_error(self, code: str) -> None:
        self.calls.append(f"handle_error:{code}")

    def clear_error(self) -> None:
        self.calls.append("clear_error")

    def log_transition(self, previous: BntState, event: BntEvent, current: BntState) -> None:
        self.transitions.append((previous, event, current))


def test_boot_enters_idle() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions)

    machine.handle(BntEvent.BOOT_COMPLETE)

    assert machine.current_state == BntState.IDLE


def test_normal_press_to_talk_flow_returns_to_idle() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions, min_recording_ms=300)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_PRESSED)
    machine.handle(BntEvent.BUTTON_RELEASED, duration_ms=400)
    machine.handle(BntEvent.SEND_SUCCEEDED, response_audio=b"wav")
    machine.handle(BntEvent.PLAYBACK_FINISHED)

    assert machine.current_state == BntState.IDLE
    assert actions.calls == [
        "start_recording",
        "stop_recording",
        "start_send",
        "start_playback:3",
    ]


def test_short_press_is_discarded_without_sending() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions, min_recording_ms=300)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_PRESSED)
    machine.handle(BntEvent.BUTTON_RELEASED, duration_ms=100)

    assert machine.current_state == BntState.IDLE
    assert "discard_recording" in actions.calls
    assert "start_send" not in actions.calls


def test_max_recording_reached_sends_audio() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_PRESSED)
    machine.handle(BntEvent.MAX_RECORDING_REACHED)

    assert machine.current_state == BntState.SENDING
    assert actions.calls[-2:] == ["stop_recording", "start_send"]


def test_backend_failure_recovers_to_idle_after_error_handled() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_PRESSED)
    machine.handle(BntEvent.BUTTON_RELEASED, duration_ms=500)
    machine.handle(BntEvent.SEND_FAILED, error_code="openai_error")
    assert machine.current_state == BntState.ERROR

    machine.handle(BntEvent.ERROR_HANDLED)

    assert machine.current_state == BntState.IDLE
    assert "handle_error:openai_error" in actions.calls
    assert "clear_error" in actions.calls


def test_send_succeeded_without_audio_enters_error() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_PRESSED)
    machine.handle(BntEvent.BUTTON_RELEASED, duration_ms=500)
    machine.handle(BntEvent.SEND_SUCCEEDED)

    assert machine.current_state == BntState.ERROR
    assert "handle_error:missing_response_audio" in actions.calls
    assert not any(call.startswith("start_playback") for call in actions.calls)


def test_timeout_recovers_to_idle_after_error_handled() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_PRESSED)
    machine.handle(BntEvent.BUTTON_RELEASED, duration_ms=500)
    machine.handle(BntEvent.TIMEOUT)
    machine.handle(BntEvent.ERROR_HANDLED)

    assert machine.current_state == BntState.IDLE
    assert "handle_error:timeout" in actions.calls


def test_button_press_during_speaking_interrupts_playback_and_records() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_PRESSED)
    machine.handle(BntEvent.BUTTON_RELEASED, duration_ms=500)
    machine.handle(BntEvent.SEND_SUCCEEDED, response_audio=b"wav")
    machine.handle(BntEvent.BUTTON_PRESSED)

    assert machine.current_state == BntState.RECORDING
    assert actions.calls[-2:] == ["stop_playback", "start_recording"]


def test_invalid_event_does_not_corrupt_state() -> None:
    actions = FakeActions()
    machine = StateMachine(actions=actions)

    machine.handle(BntEvent.BOOT_COMPLETE)
    machine.handle(BntEvent.BUTTON_RELEASED)

    assert machine.current_state == BntState.IDLE
    assert actions.calls == []
