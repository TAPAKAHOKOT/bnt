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
static constexpr uint32_t MAX_RECORDING_MS = 3000;
static constexpr size_t MAX_RECORDING_SAMPLES = SAMPLE_RATE * MAX_RECORDING_MS / 1000;
static constexpr uint32_t BEEP_DURATION_MS = 90;
static constexpr uint32_t BEEP_FREQ_HZ = 880;
static constexpr float BEEP_GAIN = 0.18f;
static constexpr int32_t MIC_GAIN = 3;
static constexpr int32_t PLAYBACK_GAIN = 1;

static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
// First-hardware response cap from the MVP audio contract.
static constexpr size_t MAX_RESPONSE_BYTES = 200 * 1024;

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

static int16_t *recordingPcm = nullptr;
static uint8_t wavHeader[WAV_HEADER_SIZE];
static size_t recordingSampleCount = 0;
static uint32_t recordingStartedAt = 0;
static uint32_t recordingPeak = 0;
static uint64_t recordingSumSquares = 0;
static bool recordingOverflow = false;

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
  config.dma_buf_count = 4;
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
  config.dma_buf_count = 4;
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

static void resetRecordingBuffer() {
  if (recordingPcm == nullptr) {
    Serial.println("[recording] cannot start: buffer allocation failed");
    recordingOverflow = true;
    return;
  }

  recordingSampleCount = 0;
  recordingStartedAt = millis();
  recordingPeak = 0;
  recordingSumSquares = 0;
  recordingOverflow = false;
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

static void playBeep() {
  Serial.println("[audio_out] beep");

  startSpeakerI2S();
  setAmpEnabled(true);
  delay(8);

  const uint32_t totalSamples = SAMPLE_RATE * BEEP_DURATION_MS / 1000;
  int16_t buffer[128 * 2];
  uint32_t produced = 0;

  while (produced < totalSamples) {
    const uint32_t count = minU32(128, totalSamples - produced);

    for (uint32_t i = 0; i < count; ++i) {
      const float phase = 2.0f * PI * BEEP_FREQ_HZ * static_cast<float>(produced + i) / SAMPLE_RATE;
      const int16_t sample = static_cast<int16_t>(sinf(phase) * 32767.0f * BEEP_GAIN);
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }

    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, buffer, count * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    produced += count;
  }

  writeSpeakerSilence(30);
  i2s_zero_dma_buffer(I2S_PORT);
  delay(10);
  setAmpEnabled(false);
  delay(10);
  stopI2S();
  silenceSpeakerPins();
}

static void playRecordedAudio() {
  if (recordingSampleCount == 0) {
    Serial.println("[audio_out] playback skipped: empty recording");
    return;
  }

  Serial.printf(
      "[audio_out] playback start bytes=%u samples=%u\n",
      static_cast<unsigned int>(recordingSampleCount * sizeof(int16_t)),
      static_cast<unsigned int>(recordingSampleCount));

  startSpeakerI2S();
  setAmpEnabled(true);
  delay(8);

  int16_t stereoBuffer[128 * 2];
  size_t played = 0;

  while (played < recordingSampleCount) {
    const uint32_t count = minU32(128, static_cast<uint32_t>(recordingSampleCount - played));

    for (uint32_t i = 0; i < count; ++i) {
      int32_t amplified = static_cast<int32_t>(recordingPcm[played + i]) * PLAYBACK_GAIN;
      if (amplified > 32767) {
        amplified = 32767;
      } else if (amplified < -32768) {
        amplified = -32768;
      }
      const int16_t sample = static_cast<int16_t>(amplified);
      stereoBuffer[i * 2] = sample;
      stereoBuffer[i * 2 + 1] = sample;
    }

    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, stereoBuffer, count * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    played += count;
  }

  writeSpeakerSilence(40);
  i2s_zero_dma_buffer(I2S_PORT);
  delay(10);
  setAmpEnabled(false);
  delay(10);
  stopI2S();
  silenceSpeakerPins();
  Serial.println("[audio_out] playback done");
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
  int32_t samples[256 * 2];
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
  size_t appended = 0;

  for (size_t i = useSlot1 ? 1 : 0; i < sampleCount; i += 2) {
    if (recordingSampleCount >= MAX_RECORDING_SAMPLES) {
      recordingOverflow = true;
      break;
    }

    const int16_t pcm = toPcm16(samples[i]);
    recordingPcm[recordingSampleCount++] = pcm;
    updateRecordingStats(pcm);
    appended++;
  }

  return {
      slot0Count == 0 ? 0 : static_cast<uint32_t>(slot0Sum / slot0Count),
      slot1Count == 0 ? 0 : static_cast<uint32_t>(slot1Sum / slot1Count),
      bytesRead,
      appended,
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
      "[recording] ms=%lu bytes=%u samples=%u volume=%lu slot0=%lu slot1=%lu read_bytes=%u appended=%u overflow=%s\n",
      static_cast<unsigned long>(durationMs),
      static_cast<unsigned int>(recordingSampleCount * sizeof(int16_t)),
      static_cast<unsigned int>(recordingSampleCount),
      static_cast<unsigned long>(activeVolume),
      static_cast<unsigned long>(stats.slot0Volume),
      static_cast<unsigned long>(stats.slot1Volume),
      static_cast<unsigned int>(stats.bytesRead),
      static_cast<unsigned int>(stats.samplesAppended),
      recordingOverflow ? "yes" : "no");
}

static void printRecordingSummary() {
  const uint32_t durationMs = recordingStartedAt == 0 ? 0 : millis() - recordingStartedAt;
  uint32_t checksum = 2166136261UL;

  if (recordingPcm != nullptr) {
    for (size_t i = 0; i < recordingSampleCount; ++i) {
      checksum ^= static_cast<uint16_t>(recordingPcm[i]);
      checksum *= 16777619UL;
    }
  }

  const uint32_t rms = recordingSampleCount == 0
                           ? 0
                           : static_cast<uint32_t>(sqrt(static_cast<double>(recordingSumSquares) / recordingSampleCount));

  Serial.printf(
      "[recording] done duration_ms=%lu bytes=%u samples=%u peak=%lu rms=%lu checksum=%08lx overflow=%s\n",
      static_cast<unsigned long>(durationMs),
      static_cast<unsigned int>(recordingSampleCount * sizeof(int16_t)),
      static_cast<unsigned int>(recordingSampleCount),
      static_cast<unsigned long>(recordingPeak),
      static_cast<unsigned long>(rms),
      static_cast<unsigned long>(checksum),
      recordingOverflow ? "yes" : "no");
}

static void writeLe16(uint8_t *dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFF);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

static void writeLe32(uint8_t *dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFF);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static uint16_t readLe16(const uint8_t *src) {
  return static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8);
}

static uint32_t readLe32(const uint8_t *src) {
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

// Build the canonical 44-byte WAV/PCM header for the recorded mono buffer.
// The PCM stays in recordingPcm; we only wrap a header so a later Wi-Fi step
// can send header bytes followed by the PCM bytes without a second big buffer.
static void buildWavHeader(uint8_t *dst, uint32_t pcmBytes) {
  const uint32_t byteRate = SAMPLE_RATE * WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8);
  const uint16_t blockAlign = WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8);

  memcpy(dst + 0, "RIFF", 4);
  writeLe32(dst + 4, 36 + pcmBytes);  // file size minus the 8-byte RIFF chunk descriptor
  memcpy(dst + 8, "WAVE", 4);

  memcpy(dst + 12, "fmt ", 4);
  writeLe32(dst + 16, 16);  // PCM fmt chunk size
  writeLe16(dst + 20, 1);   // audio format = PCM
  writeLe16(dst + 22, WAV_CHANNELS);
  writeLe32(dst + 24, SAMPLE_RATE);
  writeLe32(dst + 28, byteRate);
  writeLe16(dst + 32, blockAlign);
  writeLe16(dst + 34, WAV_BITS_PER_SAMPLE);

  memcpy(dst + 36, "data", 4);
  writeLe32(dst + 40, pcmBytes);
}

// Read back the header and confirm every field matches the MVP contract.
static bool validateWavHeader(const uint8_t *header, uint32_t pcmBytes) {
  bool ok = true;
  ok = ok && memcmp(header + 0, "RIFF", 4) == 0;
  ok = ok && memcmp(header + 8, "WAVE", 4) == 0;
  ok = ok && memcmp(header + 12, "fmt ", 4) == 0;
  ok = ok && memcmp(header + 36, "data", 4) == 0;
  ok = ok && readLe32(header + 4) == 36 + pcmBytes;
  ok = ok && readLe32(header + 16) == 16;
  ok = ok && readLe16(header + 20) == 1;
  ok = ok && readLe16(header + 22) == WAV_CHANNELS;
  ok = ok && readLe32(header + 24) == SAMPLE_RATE;
  ok = ok && readLe16(header + 34) == WAV_BITS_PER_SAMPLE;
  ok = ok && readLe32(header + 40) == pcmBytes;
  return ok;
}

static void wrapRecordingAsWav() {
  if (recordingPcm == nullptr || recordingSampleCount == 0) {
    Serial.println("[wav] skipped: empty recording");
    return;
  }

  const uint32_t pcmBytes = static_cast<uint32_t>(recordingSampleCount * sizeof(int16_t));
  buildWavHeader(wavHeader, pcmBytes);
  const bool valid = validateWavHeader(wavHeader, pcmBytes);
  const uint32_t wavBytes = WAV_HEADER_SIZE + pcmBytes;

  Serial.printf(
      "[wav] bytes=%lu pcm_bytes=%lu sample_rate=%lu channels=%u bits=%u valid=%s\n",
      static_cast<unsigned long>(wavBytes),
      static_cast<unsigned long>(pcmBytes),
      static_cast<unsigned long>(SAMPLE_RATE),
      static_cast<unsigned int>(WAV_CHANNELS),
      static_cast<unsigned int>(WAV_BITS_PER_SAMPLE),
      valid ? "yes" : "no");
}

// Light validation of the backend's WAV response against the MVP contract:
// chunk markers plus PCM/mono/16kHz/16-bit format fields. We do not assert the
// data-chunk offset because the response may carry extra chunks.
static bool validateResponseWav(const uint8_t *wav, size_t len) {
  if (len < WAV_HEADER_SIZE) {
    return false;
  }
  bool ok = true;
  ok = ok && memcmp(wav + 0, "RIFF", 4) == 0;
  ok = ok && memcmp(wav + 8, "WAVE", 4) == 0;
  ok = ok && memcmp(wav + 12, "fmt ", 4) == 0;
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

// Step 2 of the firmware/backend bridge: stream the in-RAM WAV (header + PCM)
// to the backend over a raw socket — header first, then PCM in chunks straight
// from the record buffer. This avoids a second large contiguous buffer, which
// the heap cannot supply once the recording buffer is allocated. Playback of
// the response is step 3 and is intentionally NOT done here. Failure is non-fatal.
static void sendWavToBackend() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[network] skipped: wifi not connected");
    return;
  }
  if (recordingPcm == nullptr || recordingSampleCount == 0) {
    Serial.println("[network] skipped: empty recording");
    return;
  }

  char host[64];
  char path[96];
  uint16_t port = 0;
  if (!parseBackendUrl(host, sizeof(host), &port, path, sizeof(path))) {
    Serial.println("[network] skipped: bad backend url");
    return;
  }

  const size_t pcmBytes = recordingSampleCount * sizeof(int16_t);
  const size_t wavBytes = WAV_HEADER_SIZE + pcmBytes;

  WiFiClient client;
  const uint32_t startedAt = millis();
  if (!client.connect(host, port, HTTP_TIMEOUT_MS)) {
    Serial.printf("[network] connect failed host=%s port=%u\n", host, static_cast<unsigned int>(port));
    return;
  }
  client.setTimeout(HTTP_TIMEOUT_MS / 1000);

  Serial.printf("[network] POST started bytes=%u url=%s\n",
                static_cast<unsigned int>(wavBytes), BNT_BACKEND_URL);

  // Request line + headers.
  client.printf("POST %s HTTP/1.1\r\n", path);
  client.printf("Host: %s:%u\r\n", host, static_cast<unsigned int>(port));
  client.print("Content-Type: audio/wav\r\n");
  client.printf("Content-Length: %u\r\n", static_cast<unsigned int>(wavBytes));
  client.print("Connection: close\r\n\r\n");

  // Body: 44-byte header, then PCM in chunks straight from the record buffer.
  client.write(wavHeader, WAV_HEADER_SIZE);
  const uint8_t *pcm = reinterpret_cast<const uint8_t *>(recordingPcm);
  size_t sent = 0;
  while (sent < pcmBytes) {
    size_t chunk = pcmBytes - sent;
    if (chunk > 1460) {
      chunk = 1460;
    }
    const size_t wrote = client.write(pcm + sent, chunk);
    if (wrote == 0) {
      Serial.println("[network] write stalled, aborting");
      client.stop();
      return;
    }
    sent += wrote;
  }

  // Status line: "HTTP/1.1 200 OK".
  const String statusLine = client.readStringUntil('\n');
  int statusCode = 0;
  const int sp = statusLine.indexOf(' ');
  if (sp >= 0) {
    statusCode = statusLine.substring(sp + 1, sp + 4).toInt();
  }

  // Headers: capture Content-Length and X-BNT-Text, stop at the blank line.
  long contentLength = -1;
  String bntText = "";
  while (client.connected() || client.available()) {
    const String line = client.readStringUntil('\n');
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

  const uint32_t latencyMs = millis() - startedAt;

  if (statusCode != 200) {
    Serial.printf("[network] status=%d latency_ms=%lu content_length=%ld\n",
                  statusCode, static_cast<unsigned long>(latencyMs), contentLength);
    client.stop();
    return;
  }

  if (contentLength > static_cast<long>(MAX_RESPONSE_BYTES)) {
    Serial.printf("[network] response too large: %ld > %u\n",
                  contentLength, static_cast<unsigned int>(MAX_RESPONSE_BYTES));
    client.stop();
    return;
  }

  // Read the body, keeping only the first 44 bytes for header validation and
  // counting the rest. No large response buffer is needed for step 2.
  uint8_t head[WAV_HEADER_SIZE] = {};
  size_t headFilled = 0;
  size_t bodyBytes = 0;
  uint8_t buf[256];
  const uint32_t readStart = millis();
  while (millis() - readStart < HTTP_TIMEOUT_MS) {
    const int avail = client.available();
    if (avail <= 0) {
      if (!client.connected()) {
        break;
      }
      delay(2);
      continue;
    }
    const int r = client.read(buf, sizeof(buf));
    if (r <= 0) {
      break;
    }
    for (int i = 0; i < r && headFilled < WAV_HEADER_SIZE; ++i) {
      head[headFilled++] = buf[i];
    }
    bodyBytes += static_cast<size_t>(r);
    if (contentLength > 0 && bodyBytes >= static_cast<size_t>(contentLength)) {
      break;
    }
  }
  client.stop();

  const bool valid = validateResponseWav(head, headFilled);
  Serial.printf("[network] status=200 latency_ms=%lu response bytes=%u valid=%s text=%s\n",
                static_cast<unsigned long>(latencyMs),
                static_cast<unsigned int>(bodyBytes),
                valid ? "yes" : "no",
                bntText.c_str());
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

  recordingPcm = static_cast<int16_t *>(malloc(MAX_RECORDING_SAMPLES * sizeof(int16_t)));
  Serial.printf(
      "[boot] recording buffer bytes=%u allocated=%s free_heap=%lu\n",
      static_cast<unsigned int>(MAX_RECORDING_SAMPLES * sizeof(int16_t)),
      recordingPcm == nullptr ? "no" : "yes",
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
  Serial.printf("[boot] mic_gain=%ld playback_gain=%ld\n", static_cast<long>(MIC_GAIN), static_cast<long>(PLAYBACK_GAIN));

  connectWiFi();
}

void loop() {
  static bool wasPressed = false;
  const bool pressed = readDebouncedButtonPressed();

  if (pressed && !wasPressed) {
    wasPressed = true;
    Serial.println("[button] pressed");
    playBeep();
    startMicI2S();
    resetRecordingBuffer();
  }

  if (pressed) {
    recordMicWhileHeld();
  }

  if (!pressed && wasPressed) {
    wasPressed = false;
    Serial.println("[button] released");
    stopMic();
    printRecordingSummary();
    wrapRecordingAsWav();
    sendWavToBackend();
    playRecordedAudio();
  }

  delay(5);
}
