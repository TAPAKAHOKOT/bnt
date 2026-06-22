# Testing Plan

## Purpose

Testing must prove the press-to-talk loop without requiring physical components:

```text
button event -> recording -> backend request -> response -> playback -> idle
```

Hardware integration tests come later and should not block backend, simulator, or state-machine progress.

## Test Layers

### State Machine Unit Tests

Run without hardware, audio devices, network, or OpenAI.

Coverage:

- valid transitions;
- ignored short press;
- max recording timeout;
- send success;
- send failure;
- server timeout;
- playback completion;
- playback interruption;
- error recovery.

### Backend Unit Tests

Run without simulator, firmware, hardware, or real OpenAI.

Coverage:

- accepts valid WAV;
- rejects empty body;
- rejects invalid audio;
- rejects oversized payload;
- returns MVP-format WAV with non-empty PCM frames in stub mode;
- maps fake OpenAI timeout to stable error response;
- never logs secrets.

### Simulator Integration Tests

Run without keyboard/microphone/speaker when possible.

Coverage:

- fixture audio can be sent to backend;
- fake backend response reaches fake audio output;
- local HTTP mode can call a running backend stub;
- normal flow reaches `IDLE`;
- error flow reaches `IDLE`;
- response audio bytes are non-empty.

### Firmware Fake-Hardware Tests

Run before components arrive.

Coverage:

- firmware state machine compiles and tests;
- fake button triggers transitions;
- fake audio input returns bytes;
- fake network returns response or error;
- fake audio output records play/stop calls;
- Serial logs are readable when run on board.

### Manual Simulator UX Tests

Run on a developer laptop.

Coverage:

- hold Space to record;
- release Space to send;
- response plays through laptop speaker;
- pressing Space during playback interrupts and starts new recording;
- repeated usage works at least three times.

### Manual Hardware Tests

Run only after components arrive.

Coverage:

- button debounce;
- Wi-Fi connection;
- microphone capture;
- speaker playback;
- full breadboard loop.

## Test Fixtures

Recommended files:

```text
simulator/test_audio/
  hello.wav
  silence.wav
  long_question.wav

backend/tests/fixtures/
  hello.wav
  silence.wav
  invalid.bin
```

Keep fixtures small. Do not commit private or sensitive voice recordings.

## Testing Without Physical Components

Before hardware arrives, the project should still be able to verify:

- backend endpoint behavior;
- state-machine behavior;
- full simulator flow using fixture audio;
- OpenAI integration through backend fakes;
- firmware fake-hardware behavior.

The minimum no-hardware acceptance test:

```text
fixture audio -> simulator state machine -> backend /ask-audio -> response audio -> fake playback -> IDLE
```

Run this in two modes:

- pure fake mode, where the simulator uses an in-process fake backend client;
- local HTTP mode, where the simulator sends the fixture WAV to the backend stub.

## Local Test Commands

Exact commands can be finalized during implementation, but the intended shape is:

```text
backend:   pytest backend/tests
simulator: pytest simulator/tests
firmware:  platformio test -e fake
```

## Observability Requirements

Every manual or integration test should make these visible:

- current state;
- button event;
- recording duration;
- request size;
- backend latency;
- response size;
- playback start/end;
- error code.

## Definition Of Done For Test Coverage

The MVP test suite is acceptable when:

- state-machine tests cover all required transitions;
- backend tests pass with fake OpenAI;
- simulator fixture integration passes without audio hardware;
- firmware fake-hardware tests compile and pass;
- request and response WAV files parse as mono, 16 kHz, 16-bit PCM;
- firmware fake-hardware tests validate the first hardware payload limit;
- firmware fake-hardware tests validate the first hardware response limit;
- manual simulator loop works against local backend;
- hardware tests are documented and ready before parts arrive.

## Risks and Simplifications

Risks:

- Audio device tests are hard to automate reliably.
- Firmware tests may lag if PlatformIO setup is delayed.
- Real OpenAI tests can be slow, flaky, or cost money.

Simplifications:

- Use fake OpenAI by default in automated tests.
- Use fixture audio instead of live microphone in CI/local automation.
- Keep one happy-path manual simulator test.
- Keep hardware tests manual for the first breadboard MVP.

## Out Of Scope

- Load testing.
- Multi-device concurrency testing.
- Mobile app testing.
- Battery-life testing.
- LTE testing.
- OTA testing.
- Production security testing.
