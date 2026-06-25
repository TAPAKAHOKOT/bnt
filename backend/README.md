# bnt Backend

Local FastAPI service for the MVP request-response audio contract.

Run from the repository root:

```sh
python -m uvicorn backend.app.main:app --reload
```

The endpoint is:

```http
POST /ask-audio
Content-Type: audio/wav        # or audio/L16 for raw streamed PCM
```

It accepts either a complete `audio/wav` body or raw little-endian 16-bit mono
16 kHz PCM as `audio/L16` (the firmware streams the microphone this way as a
chunked upload; the backend wraps it into the MVP WAV before processing). The
response is always MVP-format WAV (mono, 16 kHz, 16-bit).

## Response backends

The endpoint resolves a `ResponseService` per request:

- **fake** — returns a fixed sine WAV; no credentials needed.
- **openai** — transcribes the input (STT), generates a reply (chat), synthesizes
  speech (TTS), and transcodes it to the MVP WAV format (mono, 16 kHz, 16-bit).
  Short-lived multi-turn context is kept in-process per `BNT_CONVERSATION_TTL_MS`
  (default 5 minutes) so follow-up questions share conversation history.

Selection (`BNT_RESPONSE_SERVICE`):

- unset — use `openai` when `OPENAI_API_KEY` is present, otherwise `fake`.
- `fake` / `openai` — force the backend regardless of the key.

`OPENAI_API_KEY` is read from backend environment variables only and is never
logged or shipped to firmware. See `.env.example` for all OpenAI settings.
