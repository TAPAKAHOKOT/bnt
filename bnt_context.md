# bnt — Codex Project Context

This file is the base context for all Codex / coding work related to the **bnt** MVP.

Whenever Codex works on this project, it should treat this file as the source of truth for product direction, MVP scope, architecture, and development priorities.

---

## 1. Project summary

**bnt** is a small wearable physical button for talking to AI.

The device should feel like a mechanical keyboard keycap attached to clothing with a magnetic mount.

The core interaction:

1. User presses and holds the physical button.
2. User asks a question by voice.
3. User releases the button.
4. Device sends the voice request to an AI backend.
5. Device receives the answer.
6. Device speaks the answer back to the user.

The product is not a smartphone, not a smartwatch, and not another screen.

It is a **physical button for talking to AI**.

---

## 2. Product vision

The goal of bnt is to remove friction between a human and an AI assistant.

Today, asking an AI assistant usually requires many steps:

1. take out the phone;
2. unlock the phone;
3. open an app;
4. tap a button;
5. speak or type;
6. wait for the answer.

bnt reduces this to one action:

> Press → ask → hear the answer.

The main design question for every feature:

> Does this make talking to AI simpler and more natural?

If the answer is no, the feature probably does not belong in the MVP.

---

## 3. Core principles

### Simplicity

The product must be easy to understand without explanation.

Prefer fewer states, fewer settings, fewer modes, fewer buttons, and fewer dependencies.

For the MVP, avoid anything that distracts from the main flow:

> hold button → speak → release → hear response

### Invisibility

The device should feel like an accessory or part of clothing.

It should not look like a complex tech gadget.

Minimalism matters more than showing off technology.

### Tactility

Physical interaction is one of the core values.

The button should feel like a real button.

The press should feel reliable, intentional, and satisfying, similar to a mechanical keyboard key.

### Always available

AI should be accessible at any moment.

The device should be compact enough to wear every day.

### Openness

Prefer open technologies, modifiable components, and simple architecture.

Avoid unnecessary vendor lock-in where possible.

---

## 4. Current project stage

The project is in the **research and MVP prototyping** stage.

The current goal is not to build the final commercial device.

The current goal is to prove the basic experience:

> Can a small physical button record a voice question, send it to AI, and speak the answer back in a way that feels useful and natural?

Do not optimize prematurely for final PCB, final enclosure, LTE, battery life, miniaturization, or manufacturing.

Those are future stages.

---

## 5. MVP hardware components

The currently planned MVP component list:

- ESP32 CP2102 WiFi + Bluetooth, 30 pins
- INMP441 I2S microphone
- MAX98357 I2S 3W Class D amplifier
- 4Ω 3W 40mm speaker
- 6x6x4.3mm DIP tactile button
- USB A to Micro USB cable, 30cm
- Female pin header, 40 pin, 2.54mm, 1x40
- Male 90-degree pin header, 40 pin, 2.54mm
- 830-hole Arduino protoboard
- Male-to-female jumper wires, 10cm
- Male-to-male jumper wires, 10cm

Important MVP assumptions:

- Wi-Fi is enough for the first prototype.
- LTE is out of scope for the first working MVP.
- Battery is out of scope for the first software prototype.
- Final PCB is out of scope until the breadboard prototype works.
- Final enclosure is out of scope until the interaction is validated.

---

## 6. MVP user flow

The MVP must support this flow:

```text
IDLE
  ↓ button pressed
RECORDING
  ↓ button released
SENDING
  ↓ response received
SPEAKING
  ↓ playback finished
IDLE
```

Expected behavior:

- Press and hold starts recording.
- Release stops recording and sends the request.
- Device plays the AI answer through the speaker.
- While the device is speaking, pressing the button again should interrupt playback and start a new recording.
- Very short accidental presses should be ignored.
- Long recordings should auto-stop after a defined limit.
- Network/server errors should fail gracefully and return to IDLE.

Suggested initial thresholds:

- Ignore button press shorter than: `300 ms`
- Max recording length: `20 seconds`
- Server response timeout: `15 seconds`
- Audio sample rate target: `16 kHz` or `24 kHz` mono PCM for early MVP
- Playback audio: mono PCM/WAV or another simple streamable format

These values can be changed later, but the MVP should make them configurable constants.

---

## 7. Recommended architecture

Use a layered architecture.

The product logic must not depend directly on real hardware drivers.

Recommended modules:

```text
Button
AudioInput
AudioOutput
NetworkClient
StateMachine
Config
Logger
```

High-level flow:

```text
[Button]
   ↓
[StateMachine]
   ↓
[AudioRecorder] → [BackendClient] → [AudioPlayer]
```

The state machine should be the center of the product logic.

Hardware-specific code should live behind interfaces.

---

## 8. Development strategy before components arrive

The electronic components may not be available yet.

Codex should still be able to implement and test most of the MVP before hardware arrives.

### Build a desktop simulator first

Create a desktop version of bnt:

```text
keyboard spacebar = physical button
laptop microphone = INMP441 microphone
laptop speaker    = MAX98357 + physical speaker
laptop network    = ESP32 Wi-Fi
```

The simulator should support:

1. hold Space to start recording;
2. speak into laptop microphone;
3. release Space to stop recording;
4. send audio to backend;
5. receive answer;
6. play answer through laptop speakers.

This validates the product experience before soldering.

### Build firmware skeleton in parallel

The ESP32 firmware should compile even before hardware is connected.

Use fake hardware implementations where needed.

Recommended compile-time flag:

```cpp
#define BNT_FAKE_HARDWARE
```

Fake hardware examples:

- `FakeButton` — simulates button events;
- `FakeAudioInput` — reads from test WAV/PCM data or generates silence;
- `FakeAudioOutput` — logs playback or writes received audio to a debug buffer;
- `FakeNetworkClient` — returns a fixed response.

Later replace fake implementations with:

- `RealButton` — GPIO input;
- `RealINMP441` — I2S audio input;
- `RealMAX98357` — I2S audio output;
- `RealNetworkClient` — Wi-Fi HTTP/WebSocket client.

---

## 9. Suggested repository structure

Recommended initial repository structure:

```text
bnt/
  README.md
  BNT_PROJECT_CONTEXT.md
  AGENTS.md

  backend/
    README.md
    app/
      main.py
      config.py
      routes/
      services/
      audio/
      openai_client.py
    tests/

  simulator/
    README.md
    bnt_simulator/
      main.py
      button.py
      audio_input.py
      audio_output.py
      backend_client.py
      state_machine.py
      config.py
    tests/
    test_audio/

  firmware/
    README.md
    platformio.ini
    src/
      main.cpp
      config.h
      state_machine.h
      state_machine.cpp
      button.h
      button.cpp
      audio_input.h
      audio_input.cpp
      audio_output.h
      audio_output.cpp
      network_client.h
      network_client.cpp
      logger.h
      logger.cpp
    test/

  docs/
    architecture.md
    mvp-scope.md
    hardware-notes.md
    testing-plan.md
```

If the repository is still empty, create the simplest structure that supports the first working simulator and backend.

Do not create unnecessary abstractions, packages, or frameworks before they are useful.

---

## 10. Backend role

The backend is a proxy between the device/simulator and OpenAI.

The device should not store the OpenAI API key.

Recommended initial backend responsibilities:

- accept recorded audio from device/simulator;
- send request to OpenAI;
- receive text/audio response;
- return response to device/simulator;
- log basic metrics and errors.

Suggested first API:

```http
POST /ask-audio
Content-Type: audio/wav or application/octet-stream
```

Possible response options:

Option A — simple first version:

```json
{
  "text": "Answer text",
  "audio_url": "/responses/response-id.wav"
}
```

Option B — later version:

```http
200 OK
Content-Type: audio/wav
```

Start simple. Do not overbuild streaming until the basic request-response loop works.

---

## 11. Firmware responsibilities

The firmware should:

- read the physical button;
- debounce button input;
- manage state transitions;
- record audio from INMP441 over I2S;
- send audio to backend over Wi-Fi;
- receive audio response;
- play audio through MAX98357 over I2S;
- handle errors gracefully;
- keep logs readable over Serial.

The firmware should not:

- contain OpenAI API keys;
- implement complex AI prompt logic;
- depend on LTE for the first MVP;
- require a screen;
- require a mobile app for the first MVP.

---

## 12. State machine rules

The state machine must be explicit and easy to test.

Recommended states:

```text
BOOT
IDLE
RECORDING
SENDING
SPEAKING
ERROR
```

Optional future states:

```text
CONNECTING_WIFI
LOW_BATTERY
OTA_UPDATE
```

Do not add future states until needed.

### Required transitions

```text
BOOT → IDLE
IDLE → RECORDING
RECORDING → SENDING
SENDING → SPEAKING
SPEAKING → IDLE
ERROR → IDLE
```

### Interrupt behavior

If the user presses the button while the device is speaking:

```text
SPEAKING → RECORDING
```

The current audio playback should stop immediately.

This is important for the feeling of control.

---

## 13. Testing strategy

Testing should focus on the main user flow.

### Unit tests

Test the state machine without hardware.

Important scenarios:

- normal press-hold-release flow;
- press shorter than debounce threshold;
- recording timeout;
- backend timeout;
- backend error;
- empty audio;
- interrupt playback by pressing the button;
- return to IDLE after error.

### Integration tests

Use prerecorded audio files.

Examples:

```text
test_audio/
  hello.wav
  short_press.wav
  silence.wav
  long_question.wav
```

The simulator/backend integration should be able to run with these files.

### Manual UX tests

The simulator should allow manual testing:

- hold Space;
- ask a question;
- release Space;
- hear response.

Measure and log:

- recording duration;
- upload duration;
- AI response latency;
- time to first audio;
- total time until playback starts;
- error type if failed.

---

## 14. Coding preferences

Prioritize code that is:

- simple;
- readable;
- easy to test;
- easy to replace later;
- not over-engineered.

Avoid:

- unnecessary frameworks;
- complex dependency injection;
- premature streaming;
- premature LTE support;
- premature battery optimization;
- premature PCB assumptions;
- complicated setup.

Prefer explicit state machines over hidden event magic.

Prefer small modules over one large file, but do not create excessive layers.

---

## 15. Suggested initial tech choices

These are recommendations, not permanent decisions.

### Backend

Recommended:

- Python
- FastAPI
- `uv` or `pip`
- `.env` for secrets
- pytest for tests

Reason: fast to prototype, easy to inspect, easy to modify.

### Simulator

Recommended:

- Python
- keyboard input for button simulation
- local microphone recording
- local speaker playback
- HTTP client to backend

Reason: fastest way to validate UX before hardware.

### Firmware

Recommended:

- ESP32
- PlatformIO
- Arduino framework first
- Serial logging
- clear C++ classes for hardware interfaces

Reason: easier initial development and easier flashing/testing for MVP.

ESP-IDF can be considered later if Arduino becomes limiting.

---

## 16. Security and secrets

Never commit secrets.

The OpenAI API key must live only in backend environment variables.

Use something like:

```env
OPENAI_API_KEY=...
```

Provide `.env.example`, but never commit `.env`.

The ESP32 should know only the backend URL and Wi-Fi credentials for MVP.

---

## 17. Configuration

Important values should be centralized.

Suggested config values:

```text
BUTTON_DEBOUNCE_MS
MIN_RECORDING_MS
MAX_RECORDING_MS
SERVER_TIMEOUT_MS
AUDIO_SAMPLE_RATE
AUDIO_CHANNELS
AUDIO_BITS_PER_SAMPLE
BACKEND_URL
WIFI_SSID
WIFI_PASSWORD
LOG_LEVEL
```

For the simulator, use `.env` or a simple config file.

For firmware, use `config.h` or PlatformIO build flags.

---

## 18. Logging

Logs should be useful for debugging the MVP.

Log:

- state transitions;
- button events;
- recording start/stop;
- audio duration;
- request start/end;
- response size;
- playback start/end;
- error code/message.

Example:

```text
[state] IDLE → RECORDING
[audio] recording started
[audio] recording stopped, duration=2430ms
[network] POST /ask-audio started
[network] response received, status=200, latency=1840ms
[state] SENDING → SPEAKING
[audio] playback finished
[state] SPEAKING → IDLE
```

Avoid noisy per-sample logs.

---

## 19. Error handling

Errors should not trap the device in a broken state.

For MVP, every error should eventually return to `IDLE`.

Recommended behavior:

```text
error occurs
↓
short error beep or log
↓
return to IDLE
```

The MVP does not need detailed spoken error messages.

A simple beep/error tone is enough.

---

## 20. Out of scope for first MVP

Do not implement unless explicitly requested:

- LTE module support;
- battery management;
- charging circuit;
- final PCB design;
- OTA updates;
- mobile app;
- account system;
- screen;
- multiple buttons;
- wake word;
- local LLM;
- complex memory system;
- advanced assistant personality UI;
- cloud device fleet management;
- manufacturing files.

These may be important later, but they are distractions for the first working prototype.

---

## 21. MVP success criteria

The first successful MVP is achieved when:

1. user can press and hold a button;
2. device records voice;
3. device sends voice to backend;
4. backend gets AI answer;
5. device plays the answer;
6. the full loop works repeatedly;
7. errors return to IDLE;
8. the interaction feels like a physical AI button.

A breadboard with wires is acceptable.

A laptop backend is acceptable.

Wi-Fi is acceptable.

A rough speaker is acceptable.

The key is proving the interaction.

---

## 22. Product decision rule

When choosing between two options, prefer the one that makes the device:

1. simpler;
2. smaller;
3. easier to test;
4. easier to repair;
5. closer to the core interaction.

When in doubt, choose the simpler MVP path.

---

## 23. Codex behavior guidelines

When working on this repository, Codex should:

- read this file before making architecture decisions;
- preserve the MVP scope;
- avoid adding features outside the core press-to-talk loop;
- prefer simulator-first development;
- write testable code;
- keep hardware interfaces replaceable;
- avoid storing secrets in firmware;
- explain major tradeoffs briefly in code comments or docs;
- update docs when changing architecture;
- keep README instructions runnable and practical.

Codex should not:

- overcomplicate the project;
- silently introduce cloud/vendor lock-in beyond the backend/OpenAI integration;
- require hardware to test basic logic;
- assume LTE, battery, PCB, or final enclosure are already part of the MVP;
- add screens, mobile apps, or unrelated features unless explicitly requested.

---

## 24. Short project mantra

> bnt is not another gadget.
>
> bnt is a button for talking to AI.
>
> Press. Ask. Hear the answer.
