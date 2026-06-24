# bnt Backend

Local FastAPI stub for the MVP request-response audio contract.

Run from the repository root:

```sh
python -m uvicorn backend.app.main:app --reload
```

The first endpoint is:

```http
POST /ask-audio
Content-Type: audio/wav
```

## Response backends

The endpoint resolves a `ResponseService` per request:

- **fake** — returns a fixed sine WAV; no credentials needed.
- **openai** — transcribes the input (STT), generates a reply (chat), synthesizes
  speech (TTS), and transcodes it to the MVP WAV format (mono, 16 kHz, 16-bit).

Selection (`BNT_RESPONSE_SERVICE`):

- unset — use `openai` when `OPENAI_API_KEY` is present, otherwise `fake`.
- `fake` / `openai` — force the backend regardless of the key.

`OPENAI_API_KEY` is read from backend environment variables only and is never
logged or shipped to firmware. See `.env.example` for all OpenAI settings.
