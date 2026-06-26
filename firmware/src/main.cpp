#include <Arduino.h>
#include <WiFi.h>
#include <stdlib.h>
#include <string.h>
#include "driver/i2s.h"
#include "secrets.h"

// Confirmed breadboard wiring.
#define PIN_SPK_BCLK 27
#define PIN_SPK_LRC  22
#define PIN_SPK_DIN  21
#define PIN_AMP_SD   14

#define PIN_MIC_SCK 26
#define PIN_MIC_WS  25
#define PIN_MIC_SD  33

#define BUTTON_PIN 13

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr uint32_t DEBOUNCE_MS = 30;
static constexpr uint32_t VOLUME_LOG_INTERVAL_MS = 120;
static constexpr uint32_t BEEP_DURATION_MS = 160;
static constexpr float BEEP_GAIN = 0.28f;
static constexpr uint32_t REC_START_FREQ_HZ = 988;  // rising cue: recording started
static constexpr uint32_t REC_END_FREQ_HZ = 587;    // lower cue: recording stopped
// Quiet periodic "thinking" blip while waiting for the backend response.
static constexpr uint32_t THINKING_FREQ_HZ = 440;
static constexpr uint32_t THINKING_BLIP_MS = 60;
static constexpr float THINKING_GAIN = 0.05f;
static constexpr uint32_t THINKING_INTERVAL_MS = 700;
// Descending low double-beep: signals a failed backend request (offline,
// connect/timeout, or non-200 status). Deliberately lower than the record
// cues so a failure sounds distinct from normal operation.
static constexpr uint32_t ERROR_FREQ_HI_HZ = 440;
static constexpr uint32_t ERROR_FREQ_LO_HZ = 220;
static constexpr uint32_t ERROR_BEEP_MS = 200;
// After a press interrupts playback, the button must stay held at least this
// long to count as a new recording gesture; a shorter tap only stops playback.
static constexpr uint32_t INTERRUPT_HOLD_MS = 300;
static constexpr int32_t MIC_GAIN = 3;
static constexpr float PLAYBACK_GAIN = 1.0f;  // unity — no extra amplification
// Time to let the speaker I2S DMA drain to the DAC before muting. Must exceed
// the TX DMA depth (16 * 256), otherwise queued audio (a short cue, or the tail
// of a response) is cut off if we zero/stop the buffer too soon.
static constexpr uint32_t SPEAKER_DRAIN_MS = 250;
// Stereo frames read per i2s_read and streamed as one HTTP chunk. Larger =
// fewer, bigger socket writes (closer to one TCP segment) — better on weak Wi-Fi.
static constexpr size_t MIC_READ_FRAMES = 512;

static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
// The backend runs STT -> chat -> TTS via OpenAI, which can take tens of seconds
// (slow/retried calls). Wait this long for the response status before giving up.
static constexpr uint32_t BACKEND_RESPONSE_TIMEOUT_MS = 45000;
// Streaming playback tuning: abort a stalled stream fast (real-time path) and
// prefill the socket buffer before starting the DACs so the DMA ring has a lead.
static constexpr uint32_t STREAM_IDLE_TIMEOUT_MS = 2000;
static constexpr size_t STREAM_PREBUFFER_BYTES = 2048;
static constexpr uint32_t STREAM_PREBUFFER_WAIT_MS = 400;

// MVP wire format: mono 16 kHz signed 16-bit little-endian PCM in a WAV container.
static constexpr uint16_t WAV_CHANNELS = 1;
static constexpr uint16_t WAV_BITS_PER_SAMPLE = 16;
static constexpr size_t WAV_HEADER_SIZE = 44;

static constexpr i2s_comm_format_t I2S_COMM_FORMAT = I2S_COMM_FORMAT_STAND_I2S;

enum class I2SMode {
  None,
  SpeakerTx,
  MicRx,
};

static I2SMode currentI2SMode = I2SMode::None;
static bool stablePressed = false;
static bool lastRawPressed = false;
static uint32_t lastDebounceAt = 0;
static uint32_t lastVolumeLogAt = 0;

struct RecordingStats {
  uint32_t slot0Volume;
  uint32_t slot1Volume;
  size_t bytesRead;
  size_t samplesAppended;
};

// Recording is streamed to the backend over a chunked HTTP upload while the
// button is held — no audio buffer is kept, so recording length is unbounded.
static WiFiClient uploadClient;
static bool uploadActive = false;
static size_t recordingSampleCount = 0;  // total samples streamed (for stats/logs)
static uint32_t recordingStartedAt = 0;
static uint32_t recordingPeak = 0;
static uint64_t recordingSumSquares = 0;

static uint32_t minU32(uint32_t a, uint32_t b) {
  return a < b ? a : b;
}

static void setAmpEnabled(bool enabled) {
  digitalWrite(PIN_AMP_SD, enabled ? HIGH : LOW);
}

static void silenceSpeakerPins() {
  digitalWrite(PIN_SPK_BCLK, LOW);
  digitalWrite(PIN_SPK_LRC, LOW);
  digitalWrite(PIN_SPK_DIN, LOW);
}

static void stopI2S() {
  if (currentI2SMode != I2SMode::None) {
    if (currentI2SMode == I2SMode::SpeakerTx) {
      i2s_zero_dma_buffer(I2S_PORT);
    }
    i2s_stop(I2S_PORT);
    i2s_driver_uninstall(I2S_PORT);
    currentI2SMode = I2SMode::None;
  }
}

static void startSpeakerI2S() {
  if (currentI2SMode == I2SMode::SpeakerTx) {
    return;
  }

  stopI2S();

  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 16;  // ~256 ms TX cushion to absorb network jitter when streaming
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_SPK_BCLK;
  pins.ws_io_num = PIN_SPK_LRC;
  pins.data_out_num = PIN_SPK_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &config, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));

  currentI2SMode = I2SMode::SpeakerTx;
}

static void startMicI2S() {
  if (currentI2SMode == I2SMode::MicRx) {
    return;
  }

  setAmpEnabled(false);
  silenceSpeakerPins();
  stopI2S();

  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 16;  // ~128 ms RX cushion: socket writes can stall while streaming
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_MIC_SCK;
  pins.ws_io_num = PIN_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_MIC_SD;

  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &config, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));

  currentI2SMode = I2SMode::MicRx;
  lastVolumeLogAt = 0;
  delay(30);
  for (uint8_t i = 0; i < 3; ++i) {
    int32_t warmup[64];
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, warmup, sizeof(warmup), &bytesRead, pdMS_TO_TICKS(10));
  }
  Serial.println("[audio_in] mic start");
}

static void stopMic() {
  if (currentI2SMode == I2SMode::MicRx) {
    stopI2S();
  }
  setAmpEnabled(false);
  silenceSpeakerPins();
  Serial.println("[audio_in] mic stop");
}

static void resetRecordingStats() {
  recordingSampleCount = 0;
  recordingStartedAt = millis();
  recordingPeak = 0;
  recordingSumSquares = 0;
}

// Write one HTTP/1.1 chunk: "<hexlen>\r\n<bytes>\r\n". The framing and payload
// are coalesced into a single buffer so they go out as one TCP write (one
// segment with Nagle disabled) instead of three tiny writes. Returns false if
// the socket write fails (connection dropped) so the caller can stop streaming.
static bool writeHttpChunk(const uint8_t *data, size_t len) {
  static uint8_t frame[MIC_READ_FRAMES * sizeof(int16_t) + 16];
  if (len > MIC_READ_FRAMES * sizeof(int16_t)) {
    return false;  // should never happen; guards the static buffer
  }
  const int hdr = snprintf(reinterpret_cast<char *>(frame), 12, "%X\r\n", static_cast<unsigned int>(len));
  size_t total = static_cast<size_t>(hdr);
  memcpy(frame + total, data, len);
  total += len;
  frame[total++] = '\r';
  frame[total++] = '\n';

  size_t sent = 0;
  while (sent < total) {
    const size_t wrote = uploadClient.write(frame + sent, total - sent);
    if (wrote == 0) {
      return false;
    }
    sent += wrote;
  }
  return true;
}

static void writeSpeakerSilence(uint32_t durationMs) {
  int16_t silence[128 * 2] = {};
  const uint32_t totalSamples = SAMPLE_RATE * durationMs / 1000;
  uint32_t writtenSamples = 0;

  while (writtenSamples < totalSamples) {
    const uint32_t samples = minU32(128, totalSamples - writtenSamples);
    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, silence, samples * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    writtenSamples += samples;
  }
}

// Write a sine tone to the speaker I2S (assumes it is already set up + amp on).
static void writeToneToI2S(uint32_t freqHz, uint32_t durationMs, float gain) {
  const uint32_t totalSamples = SAMPLE_RATE * durationMs / 1000;
  int16_t buffer[128 * 2];
  uint32_t produced = 0;

  while (produced < totalSamples) {
    const uint32_t count = minU32(128, totalSamples - produced);

    for (uint32_t i = 0; i < count; ++i) {
      const float phase = 2.0f * PI * freqHz * static_cast<float>(produced + i) / SAMPLE_RATE;
      const int16_t sample = static_cast<int16_t>(sinf(phase) * 32767.0f * gain);
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }

    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, buffer, count * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    produced += count;
  }
}

// Two-note ascending chime to signal the device booted and is ready.
static void playReadyChime() {
  startSpeakerI2S();
  setAmpEnabled(true);
  delay(8);
  writeSpeakerSilence(60);  // let the amp soft-unmute before the tones
  writeToneToI2S(660, 120, BEEP_GAIN);
  writeToneToI2S(988, 150, BEEP_GAIN);
  writeSpeakerSilence(30);
  delay(SPEAKER_DRAIN_MS);  // let the DMA play out before muting
  setAmpEnabled(false);
  delay(10);
  stopI2S();
  silenceSpeakerPins();
}

// Standalone short cue tone: set up the speaker, play, tear down.
static void playCue(uint32_t freqHz) {
  startSpeakerI2S();
  setAmpEnabled(true);
  delay(8);
  // Pre-roll silence so the MAX98357 finishes its soft-unmute ramp BEFORE the
  // tone — otherwise a short cue is swallowed by the unmute and barely audible.
  writeSpeakerSilence(60);
  writeToneToI2S(freqHz, BEEP_DURATION_MS, BEEP_GAIN);
  writeSpeakerSilence(30);
  delay(SPEAKER_DRAIN_MS);  // let the DMA play out — do NOT zero it (that drops the cue)
  setAmpEnabled(false);
  delay(10);
  stopI2S();
  silenceSpeakerPins();
}


// Two descending low tones to signal the backend request failed. Same
// setup/teardown shape as playCue; safe to call after finishUploadAndPlay()
// has already torn the speaker down on its error paths.
static void playErrorCue() {
  startSpeakerI2S();
  setAmpEnabled(true);
  delay(8);
  writeSpeakerSilence(60);  // let the amp soft-unmute before the tones
  writeToneToI2S(ERROR_FREQ_HI_HZ, ERROR_BEEP_MS, BEEP_GAIN);
  writeToneToI2S(ERROR_FREQ_LO_HZ, ERROR_BEEP_MS, BEEP_GAIN);
  writeSpeakerSilence(30);
  delay(SPEAKER_DRAIN_MS);  // let the DMA play out before muting
  setAmpEnabled(false);
  delay(10);
  stopI2S();
  silenceSpeakerPins();
}


static uint32_t absShiftedSample(int32_t sample) {
  sample >>= 14;
  if (sample < 0) {
    sample = -sample;
  }
  return static_cast<uint32_t>(sample);
}

static int16_t toPcm16(int32_t sample) {
  sample >>= 14;
  sample *= MIC_GAIN;
  if (sample > 32767) {
    sample = 32767;
  } else if (sample < -32768) {
    sample = -32768;
  }
  return static_cast<int16_t>(sample);
}

static void updateRecordingStats(int16_t sample) {
  const int32_t value = sample;
  const uint32_t absValue = value < 0 ? static_cast<uint32_t>(-value) : static_cast<uint32_t>(value);

  if (absValue > recordingPeak) {
    recordingPeak = absValue;
  }

  recordingSumSquares += static_cast<uint64_t>(absValue) * static_cast<uint64_t>(absValue);
}

static RecordingStats readAndRecordMicChunk() {
  // Static (file-scope) to keep the loop-task stack small.
  static int32_t samples[MIC_READ_FRAMES * 2];
  size_t bytesRead = 0;

  esp_err_t result = i2s_read(I2S_PORT, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(20));
  if (result != ESP_OK || bytesRead == 0) {
    return {0, 0, bytesRead, 0};
  }

  const size_t sampleCount = bytesRead / sizeof(samples[0]);
  uint64_t slot0Sum = 0;
  uint64_t slot1Sum = 0;
  size_t slot0Count = 0;
  size_t slot1Count = 0;

  for (size_t i = 0; i < sampleCount; ++i) {
    if ((i % 2) == 0) {
      slot0Sum += absShiftedSample(samples[i]);
      slot0Count++;
    } else {
      slot1Sum += absShiftedSample(samples[i]);
      slot1Count++;
    }
  }

  const bool useSlot1 = slot1Sum > slot0Sum;
  static int16_t pcmChunk[MIC_READ_FRAMES];
  size_t produced = 0;

  for (size_t i = useSlot1 ? 1 : 0; i < sampleCount && produced < MIC_READ_FRAMES; i += 2) {
    const int16_t pcm = toPcm16(samples[i]);
    pcmChunk[produced++] = pcm;
    updateRecordingStats(pcm);
    recordingSampleCount++;
  }

  // Stream this chunk to the backend. If the socket breaks, stop streaming but
  // keep reading the mic (so stats/logs still work for the rest of the hold).
  if (uploadActive && produced > 0) {
    if (!writeHttpChunk(reinterpret_cast<const uint8_t *>(pcmChunk), produced * sizeof(int16_t))) {
      Serial.println("[network] upload chunk write failed, aborting stream");
      uploadActive = false;
    }
  }

  return {
      slot0Count == 0 ? 0 : static_cast<uint32_t>(slot0Sum / slot0Count),
      slot1Count == 0 ? 0 : static_cast<uint32_t>(slot1Sum / slot1Count),
      bytesRead,
      produced,
  };
}

static void recordMicWhileHeld() {
  const RecordingStats stats = readAndRecordMicChunk();

  if (millis() - lastVolumeLogAt < VOLUME_LOG_INTERVAL_MS) {
    return;
  }

  lastVolumeLogAt = millis();
  const uint32_t activeVolume = stats.slot0Volume > stats.slot1Volume ? stats.slot0Volume : stats.slot1Volume;
  const uint32_t durationMs = recordingStartedAt == 0 ? 0 : millis() - recordingStartedAt;
  Serial.printf(
      "[recording] ms=%lu bytes=%u samples=%u volume=%lu slot0=%lu slot1=%lu read_bytes=%u streamed=%u\n",
      static_cast<unsigned long>(durationMs),
      static_cast<unsigned int>(recordingSampleCount * sizeof(int16_t)),
      static_cast<unsigned int>(recordingSampleCount),
      static_cast<unsigned long>(activeVolume),
      static_cast<unsigned long>(stats.slot0Volume),
      static_cast<unsigned long>(stats.slot1Volume),
      static_cast<unsigned int>(stats.bytesRead),
      static_cast<unsigned int>(stats.samplesAppended));
}

static void printRecordingSummary() {
  const uint32_t durationMs = recordingStartedAt == 0 ? 0 : millis() - recordingStartedAt;
  const uint32_t rms = recordingSampleCount == 0
                           ? 0
                           : static_cast<uint32_t>(sqrt(static_cast<double>(recordingSumSquares) / recordingSampleCount));

  Serial.printf(
      "[recording] done duration_ms=%lu bytes=%u samples=%u peak=%lu rms=%lu\n",
      static_cast<unsigned long>(durationMs),
      static_cast<unsigned int>(recordingSampleCount * sizeof(int16_t)),
      static_cast<unsigned int>(recordingSampleCount),
      static_cast<unsigned long>(recordingPeak),
      static_cast<unsigned long>(rms));
}

static uint16_t readLe16(const uint8_t *src) {
  return static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8);
}

static uint32_t readLe32(const uint8_t *src) {
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

// Validate the backend's WAV response against the MVP contract. We require the
// canonical 44-byte layout (data chunk at offset 36) because streaming playback
// treats everything after byte 44 as PCM — a LIST/fact chunk before data would
// otherwise be played as noise. The backend emits exactly this layout.
static bool validateResponseWav(const uint8_t *wav, size_t len) {
  if (len < WAV_HEADER_SIZE) {
    return false;
  }
  bool ok = true;
  ok = ok && memcmp(wav + 0, "RIFF", 4) == 0;
  ok = ok && memcmp(wav + 8, "WAVE", 4) == 0;
  ok = ok && memcmp(wav + 12, "fmt ", 4) == 0;
  ok = ok && memcmp(wav + 36, "data", 4) == 0;           // PCM must start at byte 44
  ok = ok && readLe16(wav + 20) == 1;                    // PCM
  ok = ok && readLe16(wav + 22) == WAV_CHANNELS;         // mono
  ok = ok && readLe32(wav + 24) == SAMPLE_RATE;          // 16000 Hz
  ok = ok && readLe16(wav + 34) == WAV_BITS_PER_SAMPLE;  // 16-bit
  return ok;
}

// Parse BNT_BACKEND_URL ("http://host[:port]/path") into its parts.
static bool parseBackendUrl(char *host, size_t hostCap, uint16_t *port, char *path, size_t pathCap) {
  const char *url = BNT_BACKEND_URL;
  const char *prefix = "http://";
  const size_t prefixLen = strlen(prefix);
  if (strncmp(url, prefix, prefixLen) != 0) {
    return false;
  }

  const char *hostStart = url + prefixLen;
  const char *pathStart = strchr(hostStart, '/');
  const char *hostEnd = pathStart ? pathStart : hostStart + strlen(hostStart);

  const char *colon = nullptr;
  for (const char *p = hostStart; p < hostEnd; ++p) {
    if (*p == ':') {
      colon = p;
      break;
    }
  }

  const char *hostFieldEnd = colon ? colon : hostEnd;
  const size_t hostLen = static_cast<size_t>(hostFieldEnd - hostStart);
  if (hostLen == 0 || hostLen >= hostCap) {
    return false;
  }
  memcpy(host, hostStart, hostLen);
  host[hostLen] = '\0';

  *port = colon ? static_cast<uint16_t>(atoi(colon + 1)) : 80;

  if (pathStart) {
    const size_t pathLen = strlen(pathStart);
    if (pathLen >= pathCap) {
      return false;
    }
    memcpy(path, pathStart, pathLen);
    path[pathLen] = '\0';
  } else {
    if (pathCap < 2) {
      return false;
    }
    path[0] = '/';
    path[1] = '\0';
  }
  return true;
}

// Read exactly n bytes from the socket, or fewer on disconnect/idle timeout.
static size_t readFully(WiFiClient &client, uint8_t *dst, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t lastData = millis();
  while (got < n) {
    const int avail = client.available();
    if (avail <= 0) {
      if (!client.connected()) {
        break;
      }
      if (millis() - lastData > timeoutMs) {
        break;
      }
      delay(1);
      continue;
    }
    const int r = client.read(dst + got, n - got);
    if (r > 0) {
      got += static_cast<size_t>(r);
      lastData = millis();
    } else if (!client.connected()) {
      break;
    }
  }
  return got;
}

// Outcome of finishUploadAndPlay so loop() can distinguish a normal response,
// a failure (negative cue), and playback cut short by a new button press.
enum PlayOutcome { PLAY_OK, PLAY_FAILED, PLAY_INTERRUPTED };

static bool readDebouncedButtonPressed();

// Stream the WAV response body straight to the speaker without buffering the
// whole clip: read and validate the 44-byte header, then read PCM in small
// chunks and feed I2S TX as it arrives. This removes the response-length RAM
// cap — the big recording buffer is never used for playback. The enlarged I2S
// DMA buffers are the jitter cushion; a slow network can cause brief underruns.
// Returns the number of mono samples played (0 = nothing/invalid header). Sets
// `interrupted` if a button press aborted playback mid-stream.
static size_t streamResponseToSpeaker(WiFiClient &client, long contentLength, bool &interrupted) {
  uint8_t header[WAV_HEADER_SIZE];
  if (readFully(client, header, WAV_HEADER_SIZE, HTTP_TIMEOUT_MS) != WAV_HEADER_SIZE) {
    Serial.println("[audio_out] stream: short header");
    return 0;
  }
  if (!validateResponseWav(header, WAV_HEADER_SIZE)) {
    Serial.println("[audio_out] stream: invalid wav header");
    return 0;
  }

  // Remaining PCM byte budget (header already consumed). -1 = stream until close.
  long dataRemaining = contentLength > 0 ? contentLength - static_cast<long>(WAV_HEADER_SIZE) : -1;

  // Prebuffer: let the socket queue some PCM before the DACs start draining, so
  // the first reads fill the DMA ring and give a lead against the first stall.
  const uint32_t preStart = millis();
  while (client.connected() &&
         static_cast<size_t>(client.available()) < STREAM_PREBUFFER_BYTES &&
         millis() - preStart < STREAM_PREBUFFER_WAIT_MS) {
    delay(2);
  }

  startSpeakerI2S();
  setAmpEnabled(true);
  delay(8);

  int16_t out[256 * 2];
  size_t outFrames = 0;
  uint8_t in[512];
  uint8_t pendingLow = 0;
  bool havePending = false;
  size_t samplesPlayed = 0;
  uint32_t lastData = millis();

  while (dataRemaining != 0) {
    // A new button press interrupts playback (handled by loop(): a held button
    // becomes a new recording, a short tap just stops here).
    if (readDebouncedButtonPressed()) {
      Serial.println("[audio_out] stream: interrupted by button press");
      interrupted = true;
      break;
    }

    const int avail = client.available();
    if (avail <= 0) {
      if (!client.connected()) {
        break;
      }
      if (millis() - lastData > STREAM_IDLE_TIMEOUT_MS) {
        Serial.println("[audio_out] stream: idle timeout (aborting)");
        break;
      }
      delay(1);
      continue;
    }

    size_t toRead = sizeof(in);
    if (dataRemaining > 0 && static_cast<long>(toRead) > dataRemaining) {
      toRead = static_cast<size_t>(dataRemaining);
    }
    const int r = client.read(in, toRead);
    if (r <= 0) {
      if (!client.connected()) {
        break;
      }
      continue;
    }
    lastData = millis();
    if (dataRemaining > 0) {
      dataRemaining -= r;
    }

    for (int i = 0; i < r; ++i) {
      if (!havePending) {
        pendingLow = in[i];
        havePending = true;
        continue;
      }
      const int16_t sample = static_cast<int16_t>(static_cast<uint16_t>(pendingLow) |
                                                  (static_cast<uint16_t>(in[i]) << 8));
      havePending = false;

      int32_t amplified = static_cast<int32_t>(static_cast<float>(sample) * PLAYBACK_GAIN);
      if (amplified > 32767) {
        amplified = 32767;
      } else if (amplified < -32768) {
        amplified = -32768;
      }

      const int16_t s16 = static_cast<int16_t>(amplified);
      out[outFrames * 2] = s16;
      out[outFrames * 2 + 1] = s16;
      outFrames++;
      samplesPlayed++;

      if (outFrames == 256) {
        size_t wrote = 0;
        i2s_write(I2S_PORT, out, outFrames * 2 * sizeof(int16_t), &wrote, portMAX_DELAY);
        outFrames = 0;
      }
    }
  }

  if (outFrames > 0) {
    size_t wrote = 0;
    i2s_write(I2S_PORT, out, outFrames * 2 * sizeof(int16_t), &wrote, portMAX_DELAY);
  }

  if (dataRemaining > 0) {
    Serial.printf("[audio_out] stream: truncated, %ld bytes unplayed\n", dataRemaining);
  }

  if (interrupted) {
    // Snappy stop: drop queued audio instead of draining the 250 ms tail.
    i2s_zero_dma_buffer(I2S_PORT);
    delay(10);
  } else {
    writeSpeakerSilence(40);
    delay(SPEAKER_DRAIN_MS);  // let the DMA play out the response tail before muting
  }
  setAmpEnabled(false);
  delay(10);
  stopI2S();
  silenceSpeakerPins();
  return samplesPlayed;
}

// Open a chunked HTTP upload to the backend and send the request headers. The
// recorded PCM is streamed as raw audio/L16 chunks while the button is held
// (writeHttpChunk), so recording length is never bounded by RAM. Sets
// uploadActive on success. Failure is non-fatal (mic still runs for debugging).
static bool beginUpload() {
  uploadActive = false;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[network] upload skipped: wifi not connected");
    return false;
  }

  char host[64];
  char path[96];
  uint16_t port = 0;
  if (!parseBackendUrl(host, sizeof(host), &port, path, sizeof(path))) {
    Serial.println("[network] upload skipped: bad backend url");
    return false;
  }

  if (!uploadClient.connect(host, port, HTTP_TIMEOUT_MS)) {
    Serial.printf("[network] connect failed host=%s port=%u\n", host, static_cast<unsigned int>(port));
    return false;
  }
  // setTimeout is in seconds on the ESP32 Client; cover the slow OpenAI pipeline.
  uploadClient.setTimeout(BACKEND_RESPONSE_TIMEOUT_MS / 1000);
  uploadClient.setNoDelay(true);  // send each audio chunk promptly (disable Nagle)

  uploadClient.printf("POST %s HTTP/1.1\r\n", path);
  uploadClient.printf("Host: %s:%u\r\n", host, static_cast<unsigned int>(port));
  uploadClient.print("Content-Type: audio/L16;rate=16000;channels=1\r\n");
  uploadClient.print("Transfer-Encoding: chunked\r\n");
  uploadClient.printf("Authorization: Bearer %s\r\n", BNT_API_TOKEN);
  uploadClient.print("Connection: close\r\n\r\n");

  uploadActive = true;
  Serial.printf("[network] upload started (chunked) url=%s\n", BNT_BACKEND_URL);
  return true;
}

// Close the chunked upload, read the response, and stream it to the speaker.
// Returns PLAY_OK if a response played, PLAY_FAILED on error/no response, or
// PLAY_INTERRUPTED if a new button press aborted the wait/playback.
static PlayOutcome finishUploadAndPlay() {
  if (!uploadActive) {
    uploadClient.stop();
    Serial.println("[network] no upload to finish (offline or connect failed)");
    return PLAY_FAILED;
  }

  // Terminating zero-length chunk ends the request body.
  uploadClient.print("0\r\n\r\n");
  uploadActive = false;

  // Set up the speaker and play a quiet periodic "thinking" blip while the
  // backend runs STT -> chat -> TTS. streamResponseToSpeaker reuses this same
  // I2S setup (its startSpeakerI2S is a no-op) and tears it down at the end.
  startSpeakerI2S();
  setAmpEnabled(true);
  delay(8);
  {
    const uint32_t waitStart = millis();
    uint32_t lastBlip = 0;
    while (uploadClient.available() == 0 && uploadClient.connected() &&
           millis() - waitStart < BACKEND_RESPONSE_TIMEOUT_MS) {
      // Let the user bail out (and start a new question) while still waiting.
      if (readDebouncedButtonPressed()) {
        Serial.println("[network] interrupted by button while waiting for response");
        uploadClient.stop();
        i2s_zero_dma_buffer(I2S_PORT);
        delay(10);
        setAmpEnabled(false);
        delay(10);
        stopI2S();
        silenceSpeakerPins();
        return PLAY_INTERRUPTED;
      }
      const uint32_t now = millis();
      if (now - lastBlip >= THINKING_INTERVAL_MS) {
        lastBlip = now;
        writeToneToI2S(THINKING_FREQ_HZ, THINKING_BLIP_MS, THINKING_GAIN);
      } else {
        delay(5);
      }
    }
  }

  // Status line: "HTTP/1.1 200 OK".
  const String statusLine = uploadClient.readStringUntil('\n');
  int statusCode = 0;
  const int sp = statusLine.indexOf(' ');
  if (sp >= 0) {
    statusCode = statusLine.substring(sp + 1, sp + 4).toInt();
  }

  // Headers: capture Content-Length and X-BNT-Text, stop at the blank line.
  long contentLength = -1;
  String bntText = "";
  while (uploadClient.connected() || uploadClient.available()) {
    const String line = uploadClient.readStringUntil('\n');
    if (line.length() == 0 || line == "\r") {
      break;
    }
    String lower = line;
    lower.toLowerCase();
    if (lower.startsWith("content-length:")) {
      contentLength = line.substring(line.indexOf(':') + 1).toInt();
    } else if (lower.startsWith("x-bnt-text:")) {
      bntText = line.substring(line.indexOf(':') + 1);
      bntText.trim();
    }
  }

  if (statusCode != 200) {
    Serial.printf("[network] status=%d content_length=%ld\n", statusCode, contentLength);
    uploadClient.stop();
    // Tear down the speaker we set up for the thinking tone (no playback follows).
    i2s_zero_dma_buffer(I2S_PORT);
    delay(10);
    setAmpEnabled(false);
    delay(10);
    stopI2S();
    silenceSpeakerPins();
    return PLAY_FAILED;
  }

  Serial.printf("[network] status=200 content_length=%ld text=%s\n", contentLength, bntText.c_str());
  Serial.println("[audio_out] source=backend_response (streamed)");
  bool interrupted = false;
  const size_t played = streamResponseToSpeaker(uploadClient, contentLength, interrupted);
  uploadClient.stop();
  Serial.printf("[audio_out] streamed samples=%u interrupted=%d\n",
                static_cast<unsigned int>(played), interrupted ? 1 : 0);

  if (interrupted) {
    return PLAY_INTERRUPTED;
  }
  return played > 0 ? PLAY_OK : PLAY_FAILED;
}

static bool readDebouncedButtonPressed() {
  const bool rawPressed = digitalRead(BUTTON_PIN) == LOW;
  const uint32_t now = millis();

  if (rawPressed != lastRawPressed) {
    lastRawPressed = rawPressed;
    lastDebounceAt = now;
  }

  if ((now - lastDebounceAt) >= DEBOUNCE_MS && stablePressed != rawPressed) {
    stablePressed = rawPressed;
  }

  return stablePressed;
}

// On failure, scan and list the 2.4 GHz networks the ESP32 can actually see.
// If the target SSID is missing here, it is almost certainly a 5 GHz-only
// network (the ESP32 radio is 2.4 GHz only) or out of range.
static void scanVisibleNetworks() {
  Serial.println("[wifi] scanning visible 2.4GHz networks...");
  const int found = WiFi.scanNetworks();
  if (found <= 0) {
    Serial.println("[wifi] scan: no networks found");
    return;
  }
  for (int i = 0; i < found; ++i) {
    Serial.printf(
        "[wifi] scan: ssid=%s rssi=%ld ch=%d\n",
        WiFi.SSID(i).c_str(),
        static_cast<long>(WiFi.RSSI(i)),
        WiFi.channel(i));
  }
  WiFi.scanDelete();
}

// Step 1 of the firmware/backend bridge: just join the local Wi-Fi and
// report the IP. No request is sent yet. Failure is non-fatal — the device
// still runs the local record/playback loop so hardware stays usable.
static void connectWiFi() {
  Serial.printf("[wifi] connecting ssid=%s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(
        "[wifi] connected ip=%s rssi=%ld backend=%s\n",
        WiFi.localIP().toString().c_str(),
        static_cast<long>(WiFi.RSSI()),
        BNT_BACKEND_URL);
  } else {
    // status() codes: 1=NO_SSID_AVAIL (not seen / 5GHz), 4=CONNECT_FAILED
    // (often wrong password), 6=DISCONNECTED.
    Serial.printf(
        "[wifi] failed status=%d: continuing offline (local record/playback only)\n",
        static_cast<int>(WiFi.status()));
    scanVisibleNetworks();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("BNT_SERIAL_OK baud=115200");
  Serial.flush();
  delay(250);

  // Recording is streamed to the backend (chunked) — no audio buffer to allocate.
  Serial.printf("[boot] streaming recording (no buffer) free_heap=%lu\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(PIN_AMP_SD, OUTPUT);
  setAmpEnabled(false);

  pinMode(PIN_SPK_BCLK, OUTPUT);
  pinMode(PIN_SPK_LRC, OUTPUT);
  pinMode(PIN_SPK_DIN, OUTPUT);
  silenceSpeakerPins();

  Serial.println("[boot] bnt hardware check");
  Serial.println("[boot] button GPIO13 INPUT_PULLUP, pressed=LOW");
  Serial.println("[boot] speaker I2S TX GPIO27/22/21, amp SD GPIO14");
  Serial.println("[boot] mic I2S RX GPIO26/25/33, stereo diagnostic slots, INMP441 L/R=GND");
  Serial.printf("[boot] mic_gain=%ld playback_gain=%.2f\n", static_cast<long>(MIC_GAIN), PLAYBACK_GAIN);

  connectWiFi();

  Serial.println("[audio_out] cue: ready");
  playReadyChime();
}

void loop() {
  static bool wasPressed = false;
  const bool pressed = readDebouncedButtonPressed();

  if (pressed && !wasPressed) {
    wasPressed = true;
    Serial.println("[button] pressed");
    Serial.println("[audio_out] cue: record start");
    playCue(REC_START_FREQ_HZ);
    beginUpload();  // open chunked connection before recording (sets uploadActive)
    startMicI2S();
    resetRecordingStats();
  }

  if (pressed) {
    recordMicWhileHeld();  // streams each mic chunk to the open upload
    return;  // skip the idle delay; i2s_read paces the capture loop in real time
  }

  if (!pressed && wasPressed) {
    wasPressed = false;
    Serial.println("[button] released");
    stopMic();
    printRecordingSummary();
    Serial.println("[audio_out] cue: record stop");
    playCue(REC_END_FREQ_HZ);
    const PlayOutcome outcome = finishUploadAndPlay();
    if (outcome == PLAY_FAILED) {
      Serial.println("[audio_out] no response (upload failed or offline)");
      Serial.println("[audio_out] cue: error");
      playErrorCue();  // negative double-beep so the user hears the failure
    } else if (outcome == PLAY_INTERRUPTED) {
      // Playback was stopped by a new press. Keep watching the button: if the
      // user holds past the threshold, start a fresh recording (same as a
      // normal press-and-hold); a short tap that's already released just leaves
      // playback stopped.
      const uint32_t holdStart = millis();
      bool held = false;
      while (readDebouncedButtonPressed()) {
        if (millis() - holdStart >= INTERRUPT_HOLD_MS) {
          held = true;
          break;
        }
        delay(5);
      }
      if (held) {
        Serial.println("[button] held after interrupt -> start recording");
        Serial.println("[audio_out] cue: record start");
        playCue(REC_START_FREQ_HZ);
        beginUpload();
        startMicI2S();
        resetRecordingStats();
        wasPressed = true;  // continue capturing until release (normal flow)
      } else {
        Serial.println("[button] short tap -> playback interrupted only");
      }
    }
  }

  delay(5);
}
