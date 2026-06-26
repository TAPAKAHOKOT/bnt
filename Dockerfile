FROM python:3.11-slim

WORKDIR /app

# Copy only what the backend needs to build/install.
# pyproject.toml pulls fastapi/uvicorn/openai/numpy; the packages below
# are installed into site-packages so `backend.app.main:app` is importable.
COPY pyproject.toml ./
COPY bnt_core ./bnt_core
COPY backend ./backend

RUN pip install --no-cache-dir .

EXPOSE 8000

# Single worker on purpose: conversation history lives in-process memory,
# so the same process must handle the device's repeated requests.
CMD ["uvicorn", "backend.app.main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]
