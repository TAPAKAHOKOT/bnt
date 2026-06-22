# Simulator Plan

## Purpose

The desktop simulator validates the full bnt user experience before physical components arrive:

```text
hold Space -> record laptop mic -> release Space -> send backend request -> play response
```

The simulator should be the first complete product loop.

## Hardware Mapping

```text
keyboard Space       -> physical button
laptop microphone    -> INMP441 I2S microphone
laptop speaker       -> MAX98357 amplifier + speaker
laptop network       -> ESP32 Wi-Fi
local config/env      -> firmware config constants
```

## Constraints

- Use the same states and transition rules planned for firmware.
- Keep simulator-specific libraries outside the state-machine logic.
- Support prerecorded audio tests without keyboard or microphone.
- Log all state transitions and timings.
- Do not depend on real ESP32 hardware.

## Recommended Structure

```text
simulator/
  README.md
  pyproject.toml
  bnt_simulator/
    main.py
    config.py
    state_machine.py
    button.py
    audio_input.py
    audio_output.py
    backend_client.py
    logger.py
  tests/
    test_state_machine.py
    test_press_to_talk_flow.py
    test_backend_client.py
  test_audio/
    hello.wav
    silence.wav
    long_question.wav
```

## Simulator Interfaces

Keep interfaces small:

```text
Button
  emits pressed/released events

AudioInput
  start_recording()
  stop_recording() -> WAV bytes

AudioOutput
  play(WAV bytes)
  stop()

BackendClient
  ask_audio(WAV bytes) -> response WAV bytes

StateMachine
  handle(event)
  current_state
```

No simulator library should be required by the state machine.

## Operating Modes

### Manual Mode

For real UX testing:

```text
Space down -> BUTTON_PRESSED
Space up   -> BUTTON_RELEASED
```

Manual mode records from the laptop microphone and plays through laptop speakers.

### Fixture Mode

For automated tests and development without microphone access:

```text
use test_audio/hello.wav as recorded input
send to backend
play or save response audio
```

Fixture mode should run in CI/local tests and not require audio devices.

Fixture mode has two variants:

- pure fake integration: fake backend client returns fixed WAV bytes in-process;
- local HTTP integration: simulator sends fixture WAV to a running backend stub.

### Fake Backend Mode

For state-machine testing:

```text
BackendClient returns fixed response bytes
AudioOutput records that play() was called
```

This isolates product logic from network and audio libraries.

## Main Flow

```text
BOOT
  -> initialize config and dependencies
  -> IDLE

IDLE
  Space down
  -> start recording
  -> RECORDING

RECORDING
  Space up
  -> stop recording
  -> if duration < MIN_RECORDING_MS, discard and return IDLE
  -> otherwise SENDING

SENDING
  -> POST /ask-audio
  -> on success SPEAKING
  -> on error ERROR

SPEAKING
  -> play response audio
  -> playback finished IDLE
  -> Space down stops playback and enters RECORDING

ERROR
  -> log error
  -> IDLE
```

## Configuration

Use env vars or a small config file:

```text
BNT_BACKEND_URL=http://127.0.0.1:8000
BNT_MIN_RECORDING_MS=300
BNT_MAX_RECORDING_MS=20000
BNT_SERVER_TIMEOUT_MS=15000
BNT_AUDIO_SAMPLE_RATE=16000
BNT_AUDIO_CHANNELS=1
BNT_AUDIO_BITS_PER_SAMPLE=16
BNT_MAX_REQUEST_BYTES=716800
BNT_LOG_LEVEL=info
```

## Testing Without Hardware

Simulator tests should cover:

- normal press-hold-release flow;
- short press ignored;
- long recording auto-stop;
- backend timeout;
- backend error;
- playback interrupted by a new button press;
- no microphone mode using fixture audio;
- no speaker mode using fake audio output.

## Manual UX Test Checklist

For each simulator build:

1. Start local backend.
2. Start simulator.
3. Hold Space and ask a short question.
4. Release Space.
5. Confirm response audio plays.
6. Press Space during playback.
7. Confirm playback stops and new recording starts.
8. Repeat the loop three times.

Track:

- recording duration;
- upload/request duration;
- backend latency;
- time until playback starts;
- playback duration;
- error code when failed.

## Risks and Simplifications

Risks:

- Global keyboard capture can vary by OS permissions.
- Microphone libraries can be brittle across machines.
- Live audio device tests can be hard to automate.

Simplifications:

- Fixture mode is required before live microphone mode.
- Static backend response is acceptable at first.
- Saving response audio to a file is acceptable before speaker playback works.
- Use one keyboard key: Space.

## Out Of Scope

- Mobile simulator.
- Visual UI beyond minimal logs/status.
- Wake word.
- Multi-button controls.
- Streaming microphone audio to the backend.
- Conversation history UI.
