# MVP Implementation Plan

## Goal

Build the first working bnt MVP:

```text
press and hold button -> record voice -> release button -> send audio to backend -> get AI response -> play response through speaker
```

The MVP must prove the interaction before optimizing hardware, enclosure, power, or connectivity. Development is simulator-first because physical components may not have arrived yet.

## Guiding Constraints

- Keep the MVP focused on the full press-to-talk loop.
- Build and test product logic without physical hardware.
- Use an explicit state machine as the center of the product logic.
- Keep hardware drivers behind replaceable interfaces.
- Use the backend as the only OpenAI API client.
- Never store OpenAI API keys in firmware.
- Start with request-response HTTP. Do not implement streaming until the loop works.
- Use one initial wire format: `audio/wav`, mono PCM, 16 kHz, 16-bit little-endian.
- Prefer observable logs and deterministic tests over hidden event behavior.

## Recommended Repository Structure

Start with this structure and add files only when needed:

```text
bnt/
  BNT_PROJECT_CONTEXT.md
  system.md
  README.md
  .env.example

  backend/
    README.md
    pyproject.toml
    app/
      main.py
      config.py
      routes/
        ask_audio.py
      services/
        response_service.py
        fake_response_service.py
        openai_response_service.py
      audio/
        validation.py
        wav.py
      logging.py
    tests/
      test_ask_audio.py
      fixtures/
        hello.wav
        silence.wav

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
    test_audio/
      hello.wav
      silence.wav
      long_question.wav

  firmware/
    README.md
    platformio.ini
    include/
      config.h
      state_machine.h
      button.h
      audio_input.h
      audio_output.h
      network_client.h
      logger.h
    src/
      main.cpp
      state_machine.cpp
      button_fake.cpp
      audio_input_fake.cpp
      audio_output_fake.cpp
      network_client_fake.cpp
    test/
      test_state_machine/

  docs/
    mvp-implementation-plan.md
    backend-plan.md
    simulator-plan.md
    firmware-plan.md
    state-machine-plan.md
    testing-plan.md
    milestones.md
```

If the repository remains documentation-only at first, create this structure incrementally as each milestone begins. Do not scaffold unused complexity.

## MVP Architecture

```text
Simulator or Firmware
  Button
  AudioInput
  AudioOutput
  NetworkClient
  StateMachine
        |
        | HTTP POST /ask-audio
        v
Backend
  Audio validation
  OpenAI request
  Response text/audio creation
  Timing/error logs
        |
        v
OpenAI
```

The simulator and firmware should share the same product model even if they are implemented in different languages:

- button events drive state transitions;
- state transitions call audio and network capabilities;
- audio and network implementations are replaceable;
- logs make every transition visible.

## Implementation Phases

### Phase 1: Planning and Project Shape

Create docs, agree on structure, define state machine events, and define API contracts. No hardware or OpenAI call is required yet.

Definition of done:

- MVP docs exist.
- Scope and out-of-scope items are clear.
- State machine states, events, transitions, and error behavior are documented.
- Backend endpoint contract is documented.
- Simulator-first order is documented.

### Phase 2: Shared State Machine Model

Implement and test state-machine behavior in the simulator first. Mirror the same transition table in firmware tests later.

Definition of done:

- `BOOT`, `IDLE`, `RECORDING`, `SENDING`, `SPEAKING`, and `ERROR` exist.
- Normal press-hold-release path is tested.
- Short press is ignored.
- recording timeout is tested.
- backend timeout/error returns to `IDLE`.
- pressing during `SPEAKING` interrupts playback and enters `RECORDING`.

### Phase 3: Backend Stub

Create a local backend with `POST /ask-audio`. Initially return a fixed text answer and a generated or static WAV response.

Definition of done:

- Backend runs locally.
- Endpoint accepts `audio/wav` only for the MVP.
- Endpoint validates non-empty audio.
- Endpoint returns a response the simulator can play.
- Tests cover success, empty audio, unsupported format, and server error shape.

### Phase 4: Simulator Fixture Flow

Build the no-hardware integration path before live microphone and speaker work:

```text
fixture WAV -> simulator state machine -> backend stub -> response WAV -> fake audio output -> IDLE
```

Definition of done:

- Fixture test runs without keyboard, microphone, speaker, ESP32, or OpenAI.
- Request and response WAV files parse successfully.
- State machine returns to `IDLE`.
- Logs show state transitions, request size, response size, and latency.

### Phase 5: Firmware Fake-Hardware Contract Spike

Create the minimal firmware skeleton early enough to validate firmware constraints before the backend contract hardens.

Definition of done:

- PlatformIO project compiles in fake-hardware mode.
- Fake firmware can exercise the same state transitions as the simulator.
- Fake network client can represent the agreed `audio/wav` request/response shape.
- Firmware memory assumptions and max payload limits are documented.

### Phase 6: Desktop Simulator Live Loop

Build the full simulator loop with fake or local resources:

```text
Space down -> start laptop mic recording
Space up -> stop recording
POST /ask-audio -> backend
play response through laptop speaker
```

Definition of done:

- Simulator can run without ESP32 hardware.
- Manual spacebar press-to-talk loop works against the stub backend.
- Prerecorded audio can be used for automated integration tests.
- Logs show state transitions, durations, request latency, and playback start/end.

### Phase 7: Backend OpenAI Integration

Replace the fixed backend answer with an OpenAI-backed response. Keep the simulator and firmware unchanged by preserving the backend contract.

Definition of done:

- Backend reads `OPENAI_API_KEY` from environment.
- `.env.example` documents required settings without secrets.
- API key is not present in simulator or firmware.
- Backend returns AI-generated response audio or text plus audio.
- Timeout and OpenAI errors return controlled error responses.

### Phase 8: Hardware Integration

After components arrive, replace fake firmware modules one at a time:

1. `RealButton`
2. `RealNetworkClient`
3. `RealINMP441`
4. `RealMAX98357`
5. full breadboard loop

Definition of done:

- Each real component has a small manual validation step.
- Full press-to-talk loop works on breadboard over Wi-Fi.
- Errors return to `IDLE`.
- User can interrupt playback by pressing the button.

## Simplifications

- Use Wi-Fi only.
- Use USB power for firmware development.
- Use a laptop-hosted backend.
- Use a simple request-response API.
- Use `audio/wav`, mono PCM, 16 kHz, 16-bit little-endian for initial request and response bodies.
- Use one button only.
- Use simple Serial logs instead of a screen.
- Use fixed config constants before adding runtime settings.
- Use static response audio before integrating OpenAI.

## Explicitly Out Of Scope

- LTE.
- Battery management.
- Charging circuit.
- Final PCB.
- Final enclosure.
- OTA updates.
- Mobile app.
- Account system.
- Screen.
- Multiple buttons.
- Wake word.
- Local LLM.
- Advanced memory.
- Fleet management.
- Streaming responses before request-response works.

## Main Risks

- Audio format mismatch between simulator, backend, and firmware.
- Latency makes the interaction feel slow.
- ESP32 memory limits affect long audio recordings.
- I2S microphone or amplifier wiring issues slow hardware integration.
- Desktop keyboard/audio libraries behave differently across operating systems.
- OpenAI response audio format may not match firmware playback format.

## Risk Reductions

- Define one initial audio format: `audio/wav`, mono PCM, 16 kHz, 16-bit little-endian.
- Log timing for every stage.
- Keep simulator max recording length at 20 seconds or lower.
- Keep first firmware hardware recording length shorter, initially 5 seconds, until memory use is proven.
- Test backend with prerecorded audio before using live microphone input.
- Make backend able to return a known static WAV response.
- Integrate hardware one component at a time.

## First Implementation Step

Implement the backend stub and state-machine tests before live audio:

1. create the state-machine transition tests;
2. create `POST /ask-audio` returning static `audio/wav`;
3. make the simulator send prerecorded audio and play the returned response;
4. compile a fake-hardware firmware contract spike;
5. then add live microphone and keyboard handling.
