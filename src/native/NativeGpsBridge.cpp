#include <T5AppApi.h>
#include <T5GpsApi.h>

#include <Arduino.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

#if defined(BOARD_T5S3_PRO)
#include <BoardT5S3.h>
#include <TinyGPSPlus.h>
#include <memory>
#include <new>
#endif

namespace {

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

#if defined(BOARD_T5S3_PRO)
constexpr uint32_t kProbeWindowMs = 1600;
constexpr uint32_t kFreshFixMs = 5000;
constexpr uint32_t kBaudRates[] = {9600u, 38400u};

std::unique_ptr<TinyGPSPlus> parser;
bool running = false;
bool baudLocked = false;
uint8_t baudIndex = 0;
uint32_t baudStartedAt = 0;

bool powerRail(bool enabled) {
  // GPS and LoRa share PCA9535 IO0_0. This API owns that rail while GPS is active.
  if (!BoardT5S3::writePca9535Pin(PCA9535_IO00_LORA_GPS_EN, enabled)) return false;
  return BoardT5S3::setPca9535PinMode(PCA9535_IO00_LORA_GPS_EN, OUTPUT);
}

bool beginBaud(uint8_t index) {
  parser.reset(new (std::nothrow) TinyGPSPlus());
  if (!parser) return false;
  baudIndex = index % 2u;
  T5S3_SerialGPS.end();
  delay(10);
  T5S3_SerialGPS.begin(kBaudRates[baudIndex], SERIAL_8N1, T5S3_GPS_RXD, T5S3_GPS_TXD);
  while (T5S3_SerialGPS.available()) (void)T5S3_SerialGPS.read();
  baudStartedAt = millis();
  baudLocked = false;
  return true;
}

void serviceReceiver() {
  if (!running || !parser) return;
  while (T5S3_SerialGPS.available()) {
    parser->encode(static_cast<char>(T5S3_SerialGPS.read()));
  }

  if (parser->passedChecksum() > 0) {
    baudLocked = true;
  } else if (!baudLocked && millis() - baudStartedAt >= kProbeWindowMs) {
    (void)beginBaud(static_cast<uint8_t>(baudIndex ^ 1u));
  }
}
#endif

bool supported() {
#if defined(BOARD_T5S3_PRO)
  return true;
#else
  return false;
#endif
}

bool start() {
  if (!active() || !supported()) return false;
#if defined(BOARD_T5S3_PRO)
  if (running) return true;
  if (!powerRail(true)) return false;
  delay(20);
  if (!beginBaud(0)) {
    (void)powerRail(false);
    return false;
  }
  running = true;
  return true;
#else
  return false;
#endif
}

void stop() {
#if defined(BOARD_T5S3_PRO)
  if (!running) return;
  T5S3_SerialGPS.end();
  pinMode(T5S3_GPS_RXD, INPUT);
  pinMode(T5S3_GPS_TXD, INPUT);
  (void)powerRail(false);
  parser.reset();
  running = false;
  baudLocked = false;
#endif
}

bool readState(t5_gps_state_t* state) {
  if (!state) return false;
  std::memset(state, 0, sizeof(*state));
  if (!supported()) {
    state->status = T5_GPS_STATUS_UNSUPPORTED;
    return true;
  }
#if defined(BOARD_T5S3_PRO)
  if (!running || !parser) {
    state->status = T5_GPS_STATUS_OFF;
    return true;
  }

  serviceReceiver();
  state->baud = kBaudRates[baudIndex];
  state->chars_processed = static_cast<uint32_t>(parser->charsProcessed());
  state->receiver_detected = parser->passedChecksum() > 0 ? 1u : 0u;
  state->satellites = parser->satellites.isValid()
                          ? static_cast<uint8_t>(std::min<unsigned long>(parser->satellites.value(), 255ul))
                          : 0u;
  state->hdop = parser->hdop.isValid() ? static_cast<float>(parser->hdop.hdop()) : -1.0f;
  state->altitude_m = parser->altitude.isValid() ? static_cast<float>(parser->altitude.meters()) : 0.0f;
  state->speed_kph = parser->speed.isValid() ? static_cast<float>(parser->speed.kmph()) : 0.0f;
  state->course_deg = parser->course.isValid() ? static_cast<float>(parser->course.deg()) : 0.0f;

  const bool locationValid = parser->location.isValid();
  const uint32_t age = locationValid ? static_cast<uint32_t>(parser->location.age()) : UINT32_MAX;
  state->age_ms = age;
  if (locationValid && age <= kFreshFixMs) {
    state->latitude = parser->location.lat();
    state->longitude = parser->location.lng();
    state->fix_valid = 1u;
    state->status = T5_GPS_STATUS_FIX;
  } else {
    state->status = T5_GPS_STATUS_SEARCHING;
  }
  return true;
#else
  return false;
#endif
}

const t5_gps_api_v1 api = {T5_GPS_API_VERSION, sizeof(t5_gps_api_v1), supported, start, stop, readState};

}  // namespace

extern "C" const t5_gps_api_v1* t5_gps_get_api(uint32_t version) {
  return version == T5_GPS_API_VERSION && active() ? &api : nullptr;
}
