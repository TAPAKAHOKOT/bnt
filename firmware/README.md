# bnt Firmware Hardware Check

Minimal ESP32 hardware loop for the confirmed breadboard wiring.

Flow:

```text
pressed -> beep -> record PCM while held -> released -> wrap WAV
        -> POST /ask-audio -> play backend response (fallback: local recording)
```

At boot the firmware joins the local Wi-Fi, then on each press streams the
recorded WAV to the backend's `POST /ask-audio` and plays the WAV it returns.
No OpenAI key, file recording, or TTS lives in firmware — only the Wi-Fi
credentials and backend URL. Both Wi-Fi and the request are non-fatal: if
either fails, the device falls back to playing the local recording so the
hardware loop still works offline (`[audio_out] source=local_recording`).

## Wi-Fi / backend config

Credentials live in a gitignored header. Copy the example and edit it:

```sh
cp include/secrets.example.h include/secrets.h
# then edit include/secrets.h with your Wi-Fi SSID/password and backend URL
```

`include/secrets.h` is gitignored — never commit real credentials. The OpenAI
API key must NEVER live in firmware; only Wi-Fi credentials and the local
backend URL belong in `secrets.h`.

## Pinout

Button:

```cpp
#define BUTTON_PIN 13
```

Uses `INPUT_PULLUP`:

- not pressed = `HIGH`
- pressed = `LOW`

MAX98357:

```cpp
#define PIN_SPK_BCLK 27
#define PIN_SPK_LRC  22
#define PIN_SPK_DIN  21
#define PIN_AMP_SD   14
```

`PIN_AMP_SD` controls the amplifier:

- `LOW` = amplifier off
- `HIGH` = amplifier on

INMP441:

```cpp
#define PIN_MIC_SCK 26
#define PIN_MIC_WS  25
#define PIN_MIC_SD  33
```

`INMP441 L/R` is expected to be connected to `GND`, so the code reads the left I2S channel.

## Run

```sh
cd firmware
pio run -t upload
pio device monitor -b 115200
```

If the monitor shows unreadable characters such as `␀�xx`, the serial baud rate is wrong.
Run the monitor from `firmware/` or pass `-b 115200` explicitly.

If upload fails with `Invalid head of packet` or `The chip stopped responding`, force bootloader mode:

1. Close `pio device monitor`.
2. Run `pio run -t upload`.
3. When terminal shows `Connecting...`, hold the ESP32 `BOOT` button.
4. Release `BOOT` when upload starts writing.
5. If it still fails, hold `BOOT`, tap `EN/RST` once, keep holding `BOOT` for 1-2 seconds, then release.

Expected serial behavior:

```text
BNT_SERIAL_OK baud=115200
[boot] bnt hardware check
[wifi] connecting ssid=...
[wifi] connected ip=... rssi=... backend=http://.../ask-audio
[button] pressed
[audio_out] beep
[audio_in] mic start
[recording] ms=... bytes=... samples=... volume=... slot0=... slot1=... read_bytes=... appended=... overflow=no
[button] released
[audio_in] mic stop
[recording] done duration_ms=... bytes=... samples=... peak=... rms=... checksum=... overflow=no
[wav] bytes=... pcm_bytes=... sample_rate=16000 channels=1 bits=16 valid=yes
[network] POST started bytes=... url=http://.../ask-audio
[network] status=200 latency_ms=... response bytes=... valid=yes pcm_samples=... truncated=no text=...
[audio_out] source=backend_response
[audio_out] playback start bytes=... samples=...
[audio_out] playback done
```

`[audio_out] source=` shows whether playback is the `backend_response` (a 200
with a valid WAV) or `local_recording` (any network failure → offline
fallback). The backend response is decoded straight into the record buffer, so
it is capped to the 3-second buffer (`truncated=yes` if the response was
longer).

After each recording the firmware wraps the captured PCM in a canonical
44-byte WAV/PCM header (built in RAM, PCM left in place) and validates every
field against the MVP wire contract:

- `RIFF`/`WAVE`/`fmt `/`data` chunk markers present
- mono (`channels=1`), `16000` Hz, `16`-bit PCM
- header `data` size equals the recorded PCM byte count
- `bytes` = 44-byte header + `pcm_bytes`

`valid=yes` confirms the header parses against the recorded PCM. No Wi-Fi,
backend, or file write is involved yet; this only proves the WAV shape in RAM.

For INMP441 debugging:

- `bytes=0` means ESP32 is not receiving I2S samples from the driver.
- `bytes>0` and both slots stay `0` means clocks are running but the mic data slot is silent; recheck INMP441 `VDD`, `GND`, `SD -> GPIO33`, and `L/R -> GND`.
- If either `slot0` or `slot1` changes with voice, the microphone path is working.

The recording buffer is bounded to 6 seconds at 16 kHz mono 16-bit PCM:

```text
max bytes = 192000
```

This single buffer is reused for the backend response, so it also caps response
playback to 6 seconds. The size is limited by internal ESP32 RAM (a larger
buffer would starve the Wi-Fi stack); this board has no PSRAM to grow it.

If `overflow=yes`, the button was held longer than the current RAM recording limit.
Playback uses the recorded mono PCM buffer and duplicates it to both MAX98357 I2S output channels.

## Gain

Two constants control loudness/sensitivity in `src/main.cpp`:

```cpp
static constexpr int32_t MIC_GAIN = 3;
static constexpr int32_t PLAYBACK_GAIN = 1;
```

`PLAYBACK_GAIN = 2` introduced slight clipping/hiss on this wiring, so it is
kept at `1`.

`MIC_GAIN` amplifies microphone samples before storing them in RAM.
`PLAYBACK_GAIN` amplifies stored PCM only when playing it through MAX98357.
Both are clipped to signed 16-bit PCM to avoid integer overflow.
