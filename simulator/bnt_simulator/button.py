from __future__ import annotations

from dataclasses import dataclass

from bnt_core.state_machine import BntEvent


@dataclass
class FakeButton:
    events: list[BntEvent]

    @classmethod
    def press_and_release(cls) -> "FakeButton":
        return cls([BntEvent.BUTTON_PRESSED, BntEvent.BUTTON_RELEASED])
