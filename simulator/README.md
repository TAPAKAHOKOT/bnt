# bnt Simulator

Fixture mode exercises the MVP loop without keyboard, microphone, speakers, ESP32, or OpenAI.

Run with the in-process fake backend:

```sh
python -m simulator.bnt_simulator.main --fake-backend
```

Run against the local backend:

```sh
python -m uvicorn backend.app.main:app --reload
python -m simulator.bnt_simulator.main --backend-url http://127.0.0.1:8000
```

Live Space/microphone/speaker support is intentionally not implemented in this first slice.
