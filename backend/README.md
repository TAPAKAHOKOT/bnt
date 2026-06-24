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

Stub mode does not require `OPENAI_API_KEY`. Future OpenAI integration must keep the key in backend environment variables only.
