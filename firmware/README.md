# bnt Firmware Hardware Check

Minimal ESP32 hardware loop for the confirmed breadboard wiring.

Flow:

```text
pressed -> beep -> open chunked upload -> stream mic PCM while held
        -> released -> finish upload -> stream backend response to speaker
```

At boot the firmware joins the local Wi-Fi. On press it opens a chunked HTTP
upload to `POST /ask-audio` and **streams the microphone PCM as it is captured**
(`Content-Type: audio/L16`), so recording length is **not** bounded by RAM — no
audio buffer is allocated on the device. On release it finishes the upload and
**streams the backend's WAV response straight to the speaker** as it downloads,
so the response length is not RAM-bound either. No OpenAI key, file recording,
or TTS lives in firmware — only Wi-Fi credentials and the backend URL.

If Wi-Fi/upload fails, recording still runs for mic debugging but nothing is
played (`[audio_out] no response (upload failed or offline)`); there is no
offline playback fallback, since the backend is required for any answer.

Trade-off: capture and the socket write share one loop, so on a weak Wi-Fi link
a stalled write can drop some mic samples (Whisper tolerates minor glitches).
The RX DMA cushion (~128 ms), `setNoDelay`, and coalesced ~1 KB chunks mitigate
this; a dedicated TX task/ring buffer would remove it entirely if needed.

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
[audio_out] cue: record start
[audio_in] mic start
[network] upload started (chunked) url=http://.../ask-audio
[recording] ms=... bytes=... samples=... volume=... slot0=... slot1=... read_bytes=... streamed=...
[button] released
[audio_in] mic stop
[recording] done duration_ms=... bytes=... samples=... peak=... rms=...
[audio_out] cue: record stop
[network] status=200 content_length=... text=...
[audio_out] source=backend_response (streamed)
[audio_out] streamed samples=...
```

Audio cues: a short rising tone marks **recording start** and a lower tone marks
**recording stop**. While the backend runs STT→chat→TTS, the speaker plays a
quiet periodic "thinking" blip until the response begins streaming.

Neither recording nor playback is buffered in full: the mic PCM is streamed up
as it is captured, and the response is streamed to the speaker as it downloads,
so neither length is bounded by RAM. On any network failure nothing is played
(`[audio_out] no response (upload failed or offline)`) — there is no offline
playback fallback.

For INMP441 debugging (the `[recording]` lines still report mic levels even when
offline):

- `read_bytes=0` means ESP32 is not receiving I2S samples from the driver.
- `read_bytes>0` and both slots stay `0` means clocks are running but the mic data slot is silent; recheck INMP441 `VDD`, `GND`, `SD -> GPIO33`, and `L/R -> GND`.
- If either `slot0` or `slot1` changes with voice, the microphone path is working.

The response is streamed to the speaker as it downloads (see
`streamResponseToSpeaker`); the enlarged I2S TX DMA buffers provide the jitter
cushion, so on a weak Wi-Fi link a slow chunk can cause a brief audible
underrun. The mic upload shares the capture loop, so a stalled socket write can
also drop some mic samples on weak Wi-Fi (mitigated by a ~128 ms RX DMA cushion,
`setNoDelay`, and coalesced ~1 KB chunks).

## Gain

Two constants control loudness/sensitivity in `src/main.cpp`:

```cpp
static constexpr int32_t MIC_GAIN = 3;
static constexpr float PLAYBACK_GAIN = 1.5f;
```

`MIC_GAIN` amplifies microphone samples before streaming them up.
`PLAYBACK_GAIN` amplifies the response PCM before the MAX98357. `2.0` clipped on
this wiring; `1.5` is a bit louder than unity while staying mostly clean (output
is clipped to signed 16-bit, so very loud passages may distort slightly).
