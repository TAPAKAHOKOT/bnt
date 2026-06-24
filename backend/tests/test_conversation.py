from __future__ import annotations

from backend.app.services.conversation import ConversationMemory


class FakeClock:
    def __init__(self) -> None:
        self.t = 0.0

    def __call__(self) -> float:
        return self.t


def test_history_empty_initially() -> None:
    mem = ConversationMemory(ttl_seconds=300)
    assert mem.history() == []


def test_records_and_returns_turns_in_order() -> None:
    mem = ConversationMemory(ttl_seconds=300)
    mem.record("привет", "здравствуй")
    mem.record("как дела", "хорошо")
    assert mem.history() == [
        {"role": "user", "content": "привет"},
        {"role": "assistant", "content": "здравствуй"},
        {"role": "user", "content": "как дела"},
        {"role": "assistant", "content": "хорошо"},
    ]


def test_window_expires_after_ttl_from_first_message() -> None:
    clock = FakeClock()
    mem = ConversationMemory(ttl_seconds=300, clock=clock)
    mem.record("первый", "ответ")  # window opens at t=0

    clock.t = 299
    assert len(mem.history()) == 2  # still inside the 5-min window

    clock.t = 301
    assert mem.history() == []  # expired -> cleared

    # Next turn starts a fresh window anchored at the new time.
    mem.record("новый", "ответ2")
    clock.t = 400
    assert len(mem.history()) == 2


def test_bounded_to_max_messages() -> None:
    mem = ConversationMemory(ttl_seconds=300, max_messages=4)
    mem.record("a", "1")
    mem.record("b", "2")
    mem.record("c", "3")
    history = mem.history()
    assert len(history) == 4
    assert history[0]["content"] == "b"  # oldest pair dropped


def test_ttl_zero_disables_memory() -> None:
    mem = ConversationMemory(ttl_seconds=0)
    mem.record("привет", "здравствуй")
    assert mem.history() == []
