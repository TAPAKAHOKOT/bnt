from __future__ import annotations

import time
from typing import Callable

Message = dict[str, str]


class ConversationMemory:
    """In-memory multi-turn dialogue context for a single device (no DB).

    The window opens with the first recorded turn and stays valid for
    ``ttl_seconds``. Once it expires, the history is cleared and the next turn
    starts a fresh window. Bounded to ``max_messages`` to cap token cost.
    """

    def __init__(
        self,
        ttl_seconds: float,
        max_messages: int = 20,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._ttl = ttl_seconds
        self._max = max_messages
        self._clock = clock
        self._messages: list[Message] = []
        self._started_at: float | None = None

    def history(self) -> list[Message]:
        """Return the in-window history, clearing it first if the window expired."""
        if self._ttl <= 0:
            return []
        if self._started_at is not None and self._clock() - self._started_at > self._ttl:
            self.reset()
        return list(self._messages)

    def record(self, user_text: str, assistant_text: str) -> None:
        """Append a completed user/assistant turn, opening the window if needed."""
        if self._ttl <= 0:
            return
        if self._started_at is None:
            self._started_at = self._clock()
        self._messages.append({"role": "user", "content": user_text})
        self._messages.append({"role": "assistant", "content": assistant_text})
        if len(self._messages) > self._max:
            self._messages = self._messages[-self._max :]

    def reset(self) -> None:
        self._messages = []
        self._started_at = None


_shared: ConversationMemory | None = None


def get_shared_memory(ttl_seconds: float) -> ConversationMemory:
    """Process-wide memory shared across requests (the response service is
    constructed per request, so the memory must live outside it)."""
    global _shared
    if _shared is None:
        _shared = ConversationMemory(ttl_seconds)
    return _shared
