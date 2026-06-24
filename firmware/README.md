# bnt Firmware Hardware Check

Minimal ESP32 hardware loop for the confirmed breadboard wiring.

Flow:

```text
pressed -> beep -> record PCM while held -> released -> print recording stats -> play recording
```

No Wi-Fi, OpenAI, file recording, TTS, or backend code is included here.

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
[button] pressed
[audio_out] beep
[audio_in] mic start
[recording] ms=... bytes=... samples=... volume=... slot0=... slot1=... read_bytes=... appended=... overflow=no
[button] released
[audio_in] mic stop
[recording] done duration_ms=... bytes=... samples=... peak=... rms=... checksum=... overflow=no
[audio_out] playback start bytes=... samples=...
[audio_out] playback done
```

For INMP441 debugging:

- `bytes=0` means ESP32 is not receiving I2S samples from the driver.
- `bytes>0` and both slots stay `0` means clocks are running but the mic data slot is silent; recheck INMP441 `VDD`, `GND`, `SD -> GPIO33`, and `L/R -> GND`.
- If either `slot0` or `slot1` changes with voice, the microphone path is working.

The recording buffer is bounded to 3 seconds at 16 kHz mono 16-bit PCM:

```text
max bytes = 96000
```

If `overflow=yes`, the button was held longer than the current RAM recording limit.
Playback uses the recorded mono PCM buffer and duplicates it to both MAX98357 I2S output channels.

## Gain

Two constants control loudness/sensitivity in `src/main.cpp`:

```cpp
static constexpr int32_t MIC_GAIN = 3;
static constexpr int32_t PLAYBACK_GAIN = 2;
```

`MIC_GAIN` amplifies microphone samples before storing them in RAM.
`PLAYBACK_GAIN` amplifies stored PCM only when playing it through MAX98357.
Both are clipped to signed 16-bit PCM to avoid integer overflow.
