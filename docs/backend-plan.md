# Backend Plan

## Purpose

The backend is the secure proxy between simulator/device and OpenAI. It owns all OpenAI credentials and hides AI-provider details from firmware.

The first backend must support:

```text
receive audio -> call AI or stub service -> return MVP-format WAV response -> log timing/errors
```

## Constraints

- Do not store OpenAI API keys in firmware.
- Do not require accounts or device registration for MVP.
- Do not implement streaming first.
- Keep the endpoint simple enough for ESP32 HTTP clients.
- Preserve the same API contract for simulator and firmware.

## Recommended Tech

- Python.
- FastAPI.
- `uv` or `pip` for local development.
- `pytest` for tests.
- `.env` for secrets.

## Recommended Structure

```text
backend/
  README.md
  pyproject.toml
  .env.example
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
    test_audio_validation.py
    fixtures/
      hello.wav
      silence.wav
```

## API Contract

## MVP Audio Contract

Use one wire format until the MVP loop works:

```text
Content-Type: audio/wav
Encoding: PCM signed 16-bit little-endian
Channels: 1
Sample rate: 16000 Hz
Request max duration: 20 seconds from simulator, 5 seconds from first firmware hardware pass
Backend request max size: 700 KB
Firmware local request max size: 200 KB for first hardware pass
Backend response max duration: 5 seconds for all MVP clients until hardware proves more
Backend response max size: 200 KB for all MVP clients until hardware proves more
Firmware local response max size: 200 KB for first hardware pass
```

The backend may transcode internally, but simulator and firmware should only need to send and receive this WAV shape for the MVP.

The backend should enforce the global 700 KB request limit and the 200 KB response limit. The firmware should enforce the smaller first-hardware 200 KB request limit locally before sending audio and should reject responses above 200 KB. Do not add client-specific backend policy until the MVP needs it.

### First Endpoint

```http
POST /ask-audio
Content-Type: audio/wav
```

Request body:

- WAV bytes for the first version.
- Mono, 16 kHz, 16-bit PCM little-endian.
- Backend should reject empty audio and obviously invalid content.

Recommended first response:

```http
200 OK
Content-Type: audio/wav
X-BNT-Text: optional short text answer
X-BNT-Request-Id: generated request id
```

Body:

- MVP-format WAV bytes that parse successfully and contain non-empty PCM frames.

This plan deliberately chooses a direct WAV response over the earlier JSON-plus-`audio_url` option to keep the firmware request-response path simpler. JSON plus `audio_url` can be added later if debugging requires it.

### Error Response

```json
{
  "error": {
    "code": "empty_audio",
    "message": "Audio payload is empty",
    "request_id": "..."
  }
}
```

Use stable error codes:

- `empty_audio`
- `invalid_audio`
- `payload_too_large`
- `openai_timeout`
- `openai_error`
- `internal_error`

Error response details:

| Code | HTTP status | Content-Type | State-machine event | Device behavior |
| --- | --- | --- | --- | --- |
| `empty_audio` | `400` | `application/json` | `SEND_FAILED` | log and return to `IDLE` |
| `invalid_audio` | `400` | `application/json` | `SEND_FAILED` | log and return to `IDLE` |
| `payload_too_large` | `413` | `application/json` | `SEND_FAILED` | log and return to `IDLE` |
| `openai_timeout` | `504` | `application/json` | `TIMEOUT` | log and return to `IDLE` |
| `openai_error` | `502` | `application/json` | `SEND_FAILED` | log and return to `IDLE` |
| `internal_error` | `500` | `application/json` | `SEND_FAILED` | log and return to `IDLE` |

Firmware does not need rich error UI for MVP. It may parse only status code plus error code when available, then emit the matching state-machine event.

## Backend Flow

```text
receive request
  -> assign request_id
  -> validate content type and size
  -> validate readable WAV/audio shape
  -> call response service
  -> return WAV response
  -> log timing and result
```

## Development Stages

### Stage 1: Stubbed Response

Return a fixed WAV response without OpenAI. This lets simulator and firmware develop against a stable endpoint.

Definition of done:

- `POST /ask-audio` accepts a fixture WAV.
- Response body parses as MVP-format WAV and has non-empty PCM frames.
- Empty audio returns `400`.
- Invalid audio returns `400`.
- Logs include request id, request size, latency, and status.

### Stage 2: Text AI Response

Send audio to OpenAI for transcription and response text, then return a simple generated audio file. If direct TTS is not ready, return a fixed WAV while logging the generated text.

Keep OpenAI behind a backend service boundary:

```text
ResponseService.generate_response_audio(input_wav_bytes) -> output_wav_bytes
```

Automated tests should use a fake `ResponseService`.

Definition of done:

- `OPENAI_API_KEY` is read from backend environment only.
- Backend can produce an AI text answer from input audio.
- API contract remains unchanged for the simulator.
- OpenAI timeout is handled cleanly.

### Stage 3: AI Audio Response

Return OpenAI-generated response audio in the agreed format or transcode it to the agreed MVP playback format. For the MVP, generated response audio must be capped to the firmware-safe 5-second / 200 KB limit even when the simulator could handle more.

Definition of done:

- Response parses as MVP-format WAV, has non-empty PCM frames, and simulator playback starts without error.
- Response duration and size stay within the MVP backend response cap.
- Backend logs OpenAI latency separately from total request latency.
- Tests can still run without real OpenAI by using a fake service.

## Configuration

Use environment variables:

```text
OPENAI_API_KEY
BNT_RESPONSE_TIMEOUT_MS
BNT_MAX_REQUEST_BYTES
BNT_MAX_RESPONSE_BYTES
BNT_MAX_RESPONSE_DURATION_MS
BNT_AUDIO_SAMPLE_RATE
BNT_AUDIO_CHANNELS
BNT_AUDIO_BITS_PER_SAMPLE
BNT_LOG_LEVEL
```

Provide `.env.example` with placeholder values only.

## Logging

Log one line per major stage:

```text
[request] id=... POST /ask-audio bytes=...
[audio] id=... duration_ms=... sample_rate=...
[openai] id=... latency_ms=... status=ok
[response] id=... bytes=... total_latency_ms=...
[error] id=... code=... message=...
```

Do not log secrets or full audio content.

## Testing Without Hardware

- Unit-test audio validation with fixture files.
- Test endpoint with `TestClient`.
- Use a fake OpenAI service for deterministic tests.
- Include one success fixture, one silence fixture, one invalid payload, and one oversized payload.
- Verify response content type and non-empty response bytes.

## Integration Strategy

Before hardware:

1. simulator sends prerecorded WAV to backend;
2. backend returns static WAV;
3. firmware fake network validates the same request/response shape;
4. simulator sends live microphone WAV to backend;
5. backend returns AI WAV.

After hardware:

1. firmware sends fake MVP-format WAV;
2. firmware sends real microphone recording;
3. firmware plays returned backend audio.

## Risks and Simplifications

Risks:

- OpenAI audio output format may not match ESP32 playback requirements.
- Large audio payloads may be slow or memory-heavy.
- Request-response latency may feel slow.

Simplifications:

- Start with WAV.
- Keep max recording duration short.
- Use a laptop backend on the same Wi-Fi network.
- Return static WAV until the simulator loop is stable.

## Out Of Scope

- User accounts.
- Device registration.
- Persistent conversation memory.
- Streaming audio.
- Web dashboard.
- Cloud deployment automation.
- API keys in firmware.
