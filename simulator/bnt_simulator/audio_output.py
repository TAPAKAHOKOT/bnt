from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class FakeAudioOutput:
    played_audio: list[bytes] = field(default_factory=list)
    stopped: bool = False
    fail_playback: bool = False

    def play(self, audio: bytes) -> None:
        if self.fail_playback:
            raise RuntimeError("fake playback failure")
        self.played_audio.append(audio)
        self.stopped = False

    def stop(self) -> None:
        self.stopped = True
