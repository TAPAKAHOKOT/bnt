# bnt

Simulator-first MVP prototype for a wearable physical button for talking to AI.

The first implementation slice is intentionally small:

- shared, testable MVP state machine;
- fake button/audio/network components for no-hardware tests;
- FastAPI backend stub with `POST /ask-audio`;
- simulator fixture entry point that runs the press-to-talk loop without electronics.

Out of scope for this slice: firmware hardware drivers, streaming, LTE, battery, PCB, OTA, mobile app, wake word, screen, and account system.

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

## Firmware Hardware Check

The current breadboard-only ESP32 check lives in `firmware/`.

It verifies this simple hardware loop:

```text
pressed -> beep -> record PCM while held -> released -> print recording stats -> play recording
```

Run it with PlatformIO:

```sh
cd firmware
pio run -t upload
pio device monitor -b 115200
```

This firmware check intentionally does not include Wi-Fi, OpenAI, file recording, TTS, or backend calls.
