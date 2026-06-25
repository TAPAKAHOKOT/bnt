# bnt

Simulator-first MVP prototype for a wearable physical button for talking to AI.

What is implemented today:

- shared, testable MVP state machine (`bnt_core`);
- FastAPI backend with `POST /ask-audio` that returns MVP-format WAV, backed by either a fake sine response or the OpenAI STT → chat → TTS pipeline (with short-lived multi-turn context);
- simulator fixture entry point that runs the press-to-talk loop without electronics (fake audio in/out);
- ESP32 breadboard firmware that streams the microphone to the backend over Wi-Fi and streams the spoken answer back to the speaker.

Out of scope: LTE, battery, PCB, OTA, mobile app, wake word, screen, and account system. Live Spacebar/microphone/speaker support in the simulator is also not implemented yet.

## Setup

Use Python 3.11 or newer.

```sh
python -m venv .venv
source .venv/bin/activate
python -m pip install -e ".[dev]"
```

Use `.env.example` as a reference for local environment variables. Export any overrides in your shell before starting the backend or simulator. Do not commit `.env` or real secrets.

## Run Tests

```sh
pytest
```

Useful subsets:

```sh
pytest tests
pytest backend/tests
pytest simulator/tests
```

## Start Backend

```sh
python -m uvicorn backend.app.main:app --reload
```

Health check:

```sh
curl http://127.0.0.1:8000/health
```

## Start Simulator

Fixture mode with an in-process fake backend:

```sh
python -m simulator.bnt_simulator.main --fake-backend
```

Fixture mode against the local backend stub:

```sh
python -m uvicorn backend.app.main:app --reload
python -m simulator.bnt_simulator.main --backend-url http://127.0.0.1:8000
```

The simulator currently uses fake audio input/output. Live Spacebar, microphone, and speaker support belongs to the next simulator milestone.

## Firmware

The breadboard ESP32 firmware lives in `firmware/`.

At boot it joins Wi-Fi and plays a ready chime. The press-to-talk loop is:

```text
pressed -> record-start cue -> stream mic PCM to backend while held
        -> released -> record-stop cue -> stream backend WAV response to speaker
```

The microphone is streamed up as chunked `audio/L16` and the response is streamed straight to the speaker, so neither length is bounded by device RAM. No OpenAI key or TTS lives in firmware — only the Wi-Fi credentials and backend URL. On any network failure nothing is played (there is no offline playback fallback).

Run it with PlatformIO:

```sh
cd firmware
pio run -t upload
pio device monitor -b 115200
```

See `firmware/README.md` for wiring, pinout, gain constants, and serial output details.
