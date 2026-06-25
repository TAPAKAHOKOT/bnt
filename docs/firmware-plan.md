# Firmware Plan

## Purpose

Firmware turns the MVP interaction into a breadboard ESP32 prototype after the simulator proves the loop.

The firmware should:

```text
read button -> record INMP441 audio -> POST to backend -> play response through MAX98357
```

It must also compile and test with fake hardware before components are connected.

## Implementation Status

The shipped firmware diverges from the modular fake/real design below — that
layered structure and the `BNT_FAKE_HARDWARE` flag were never built. What exists
today is a single `src/main.cpp` breadboard hardware check that runs directly on
real components:

- joins Wi-Fi at boot and plays a ready chime;
- on press, opens a chunked HTTP upload and streams the INMP441 PCM as raw
  `audio/L16` while the button is held (no bounded WAV buffer is allocated);
- on release, finishes the upload and streams the backend's WAV response straight
  to the MAX98357 as it downloads.

So the "bounded 5-second WAV buffers" and per-module fake/real files in the rest
of this plan describe the original intent, not the current code. The sections
below are kept as design history.

## Constraints

- No OpenAI API key in firmware.
- Wi-Fi only for MVP.
- USB power is acceptable.
- No LTE, battery, screen, OTA, mobile app, or account system.
- Keep product logic separate from hardware drivers.
- Return to `IDLE` after errors.

## Recommended Tech

- ESP32.
- PlatformIO.
- Arduino framework for the first MVP.
- C++ classes or structs for interfaces.
- Serial logging.

## Recommended Structure

```text
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
    button_gpio.cpp
    audio_input_inmp441.cpp
    audio_output_max98357.cpp
    network_client_wifi.cpp
    logger_serial.cpp
  test/
    test_state_machine/
    test_fake_flow/
```

Real hardware files can be added only when integration begins. The first firmware milestone can include fake files only.

## Compile-Time Modes

Use a build flag:

```text
BNT_FAKE_HARDWARE
```

Fake mode:

- fake button events;
- fake audio bytes or silence;
- fake network response;
- fake audio output logs playback.

Real mode:

- GPIO button;
- INMP441 I2S input;
- Wi-Fi HTTP client;
- MAX98357 I2S output.

## Firmware Modules

### Button

Responsibilities:

- read pressed/released state;
- debounce;
- emit stable button events.

Initial behavior:

- do not decide whether a recording is too short;
- emit stable button press/release events for the state machine;
- use clear Serial logs for press and release.

### AudioInput

Responsibilities:

- start recording;
- stop recording;
- provide recorded audio in the agreed MVP format.

Initial target:

- WAV containing mono PCM;
- 16 kHz;
- 16-bit little-endian;
- first real-hardware request max 5 seconds;
- first real-hardware response max 5 seconds;
- simulator/backend max 20 seconds.

Memory rule:

- do not assume a basic ESP32 can hold long request and response audio in RAM at the same time;
- first hardware pass should target about 160 KB input audio plus WAV header;
- first hardware pass should target about 160 KB response audio plus WAV header;
- first firmware implementation should use whole bounded 5-second WAV buffers only;
- firmware should release request audio before buffering response audio when possible;
- file-backed buffering or HTTP body chunk handling is a later fallback only if bounded 5-second buffers are proven insufficient.
- simple bounded buffering needed for the MVP is in scope; production memory optimization and advanced memory features are out of scope.

### NetworkClient

Responsibilities:

- send audio to backend;
- enforce timeout;
- return bounded MVP-format response audio bytes or an error code.

Firmware knows:

- backend URL;
- Wi-Fi credentials.

Firmware does not know:

- OpenAI API key;
- OpenAI model details;
- assistant prompt internals.

### AudioOutput

Responsibilities:

- play returned audio;
- stop immediately when interrupted;
- report playback completion.

### StateMachine

Responsibilities:

- own current state;
- accept events;
- enforce valid transitions;
- coordinate module actions.

The state machine should not import Wi-Fi, I2S, or GPIO details.

## Fake Hardware Strategy

Fake hardware must make firmware useful before parts arrive:

- `FakeButton`: scheduled press/release events or Serial-command-triggered events.
- `FakeAudioInput`: returns fixed MVP-format WAV bytes, generated silence, or a short embedded fixture.
- `FakeNetworkClient`: returns fixed response bytes and can simulate timeout/error.
- `FakeAudioOutput`: logs `play`, `stop`, and `finished` events.

Fake mode should prove:

- normal transition sequence;
- short press handling;
- timeout/error handling;
- playback interruption.

## Hardware Integration Order

After components arrive, integrate one module at a time:

1. Real button on GPIO.
2. Wi-Fi network client calling local backend.
3. INMP441 recording with a local debug upload using the MVP WAV shape.
4. MAX98357 playback using a known MVP-format WAV response.
5. Full loop with real button, microphone, backend, and speaker.

Do not debug microphone and speaker together before each works alone.

## Logging

Serial logs should include:

```text
[state] IDLE -> RECORDING
[button] pressed
[button] released duration_ms=...
[audio_in] bytes=... duration_ms=...
[network] POST started bytes=...
[network] status=200 latency_ms=...
[audio_out] play bytes=...
[error] code=... message=...
```

Avoid per-sample logs.

## Testing Without Components

- Compile fake firmware.
- Run state-machine unit tests.
- Run fake flow tests.
- Use fake network timeout and error cases.
- Verify logs manually on Serial once board is available, even before microphone/speaker wiring.

## Risks and Simplifications

Risks:

- ESP32 RAM may limit full WAV buffering.
- I2S configuration can be sensitive.
- Backend audio response format may not match the MVP-format WAV contract unless validated or transcoded.
- Wi-Fi upload latency may make the loop feel slow.

Simplifications:

- Limit max recording length.
- Prefer one simple audio format.
- Use local Wi-Fi and laptop backend.
- Use request-response before streaming.
- Use USB power.
- Use fake hardware until each real module is ready.

## Out Of Scope

- LTE.
- Battery management.
- Charging.
- Final PCB.
- OTA.
- Screen.
- Mobile provisioning app.
- Wake word.
- Local AI.
- Device account pairing.
