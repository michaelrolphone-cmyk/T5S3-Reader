#include <T5AppApi.h>
#include <T5LoRaApi.h>

#include <Arduino.h>
#include <cstdint>
#include <cstring>

#if defined(BOARD_T5S3_PRO)
#include <BoardT5S3.h>
#include <RadioLib.h>
#include <SPI.h>
#endif

namespace {

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

void fillDefaultConfig(t5_lora_config_t* config) {
  if (!config) return;
  std::memset(config, 0, sizeof(*config));
  config->frequency_hz = 915000000u;
  config->bandwidth_hz = 125000u;
  config->preamble_symbols = 8u;
  config->spreading_factor = 10u;
  config->coding_rate = 5u;
  config->sync_word = 0x12u;
  config->tx_power_dbm = 22;
  config->crc_enabled = 1u;
}

#if defined(BOARD_T5S3_PRO)
Module radioModule(T5S3_LORA_CS, T5S3_LORA_IRQ, T5S3_LORA_RST, T5S3_LORA_BUSY, SPI,
                   SPISettings(8000000, MSBFIRST, SPI_MODE0));
SX1262 radio(&radioModule);

volatile bool packetReceived = false;
bool running = false;
bool receiverActive = false;
bool pausedForDisplay = false;
int16_t lastError = RADIOLIB_ERR_NONE;
uint32_t packetsReceived = 0;
uint32_t packetsSent = 0;
t5_lora_config_t currentConfig{};

void IRAM_ATTR onPacketReceived() { packetReceived = true; }

bool powerRail(bool enabled) {
  if (!BoardT5S3::writePca9535Pin(PCA9535_IO00_LORA_GPS_EN, enabled)) return false;
  return BoardT5S3::setPca9535PinMode(PCA9535_IO00_LORA_GPS_EN, OUTPUT);
}

void releaseRadioPins() {
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_LORA_RST, OUTPUT);
  digitalWrite(T5S3_LORA_RST, LOW);
  pinMode(T5S3_LORA_IRQ, INPUT);
  pinMode(T5S3_LORA_BUSY, INPUT);
}

bool startReceiver() {
  if (!running) return false;
  packetReceived = false;
  radio.setPacketReceivedAction(onPacketReceived);
  const int16_t state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    radio.clearPacketReceivedAction();
    receiverActive = false;
    lastError = state;
    return false;
  }
  receiverActive = true;
  return true;
}

void shutdownHardware() {
  if (running) {
    radio.clearPacketReceivedAction();
    (void)radio.sleep();
  }
  receiverActive = false;
  packetReceived = false;
  running = false;
  releaseRadioPins();
  (void)powerRail(false);
}

bool initializeHardware(const t5_lora_config_t* config) {
  if (!config) return false;
  currentConfig = *config;
  receiverActive = false;
  packetReceived = false;
  running = false;
  lastError = RADIOLIB_ERR_NONE;

  // LoRa and SD share SPI. Keep both chip selects inactive before enabling
  // the shared GPS/LoRa 3.3 V rail, matching LilyGO's board bring-up sequence.
  BoardT5S3::prepareSdBus();
  if (!powerRail(true)) {
    lastError = -1;
    return false;
  }
  delay(1500);
  BoardT5S3::prepareSdBus();

  const float frequencyMhz = static_cast<float>(config->frequency_hz) / 1000000.0f;
  const float bandwidthKhz = static_cast<float>(config->bandwidth_hz) / 1000.0f;
  int16_t state = radio.begin(frequencyMhz, bandwidthKhz, config->spreading_factor, config->coding_rate,
                              config->sync_word, config->tx_power_dbm, config->preamble_symbols,
                              2.4f, false);
  if (state == RADIOLIB_ERR_NONE) state = radio.setCurrentLimit(140.0f);
  if (state == RADIOLIB_ERR_NONE) state = radio.setCRC(config->crc_enabled != 0);
  if (state == RADIOLIB_ERR_NONE) state = radio.setDio2AsRfSwitch();
  if (state != RADIOLIB_ERR_NONE) {
    lastError = state;
    shutdownHardware();
    lastError = state;
    return false;
  }

  running = true;
  if (!startReceiver()) {
    const int16_t receiveError = lastError;
    shutdownHardware();
    lastError = receiveError;
    return false;
  }
  return true;
}
#endif

bool supported() {
#if defined(BOARD_T5S3_PRO)
  return true;
#else
  return false;
#endif
}

bool start(const t5_lora_config_t* config) {
  if (!active() || !supported()) return false;
#if defined(BOARD_T5S3_PRO)
  if (running) return true;
  t5_lora_config_t defaults{};
  if (!config) {
    fillDefaultConfig(&defaults);
    config = &defaults;
  }
  packetsReceived = 0;
  packetsSent = 0;
  pausedForDisplay = false;
  return initializeHardware(config);
#else
  (void)config;
  return false;
#endif
}

void stop() {
#if defined(BOARD_T5S3_PRO)
  pausedForDisplay = false;
  shutdownHardware();
  lastError = RADIOLIB_ERR_NONE;
#endif
}

bool readState(t5_lora_state_t* state) {
  if (!state) return false;
  std::memset(state, 0, sizeof(*state));
  if (!supported()) {
    state->status = T5_LORA_STATUS_UNSUPPORTED;
    fillDefaultConfig(&state->config);
    return true;
  }
#if defined(BOARD_T5S3_PRO)
  state->config = currentConfig;
  state->last_error = lastError;
  state->packets_received = packetsReceived;
  state->packets_sent = packetsSent;
  state->receiver_active = receiverActive ? 1u : 0u;
  if (running) state->status = T5_LORA_STATUS_READY;
  else if (lastError != RADIOLIB_ERR_NONE) state->status = T5_LORA_STATUS_ERROR;
  else state->status = T5_LORA_STATUS_OFF;
  return true;
#else
  return false;
#endif
}

bool pollPacket(t5_lora_packet_t* packet) {
  if (!packet) return false;
  std::memset(packet, 0, sizeof(*packet));
#if defined(BOARD_T5S3_PRO)
  if (!running || !receiverActive || !packetReceived) return false;
  packetReceived = false;

  const size_t length = radio.getPacketLength();
  if (length == 0 || length > T5_LORA_MAX_PACKET) {
    lastError = -2;
    (void)startReceiver();
    return false;
  }

  const int16_t state = radio.readData(packet->data, length);
  receiverActive = false;
  if (state != RADIOLIB_ERR_NONE) {
    lastError = state;
    (void)startReceiver();
    return false;
  }

  packet->length = static_cast<uint16_t>(length);
  packet->rssi_dbm_x10 = static_cast<int16_t>(radio.getRSSI() * 10.0f);
  packet->snr_db_x10 = static_cast<int16_t>(radio.getSNR() * 10.0f);
  packet->frequency_error_hz = static_cast<int32_t>(radio.getFrequencyError());
  packet->received_at_ms = millis();
  ++packetsReceived;
  lastError = RADIOLIB_ERR_NONE;
  (void)startReceiver();
  return true;
#else
  return false;
#endif
}

bool transmit(const uint8_t* data, uint16_t length) {
  if (!data || length == 0 || length > T5_LORA_MAX_PACKET) return false;
#if defined(BOARD_T5S3_PRO)
  if (!running) return false;
  radio.clearPacketReceivedAction();
  receiverActive = false;
  packetReceived = false;
  (void)radio.standby();

  const int16_t state = radio.transmit(data, length);
  if (state == RADIOLIB_ERR_NONE) {
    ++packetsSent;
    lastError = RADIOLIB_ERR_NONE;
  } else {
    lastError = state;
  }
  const bool receiverRestored = startReceiver();
  return state == RADIOLIB_ERR_NONE && receiverRestored;
#else
  return false;
#endif
}

bool prepareDisplay() {
  if (!active()) return false;
#if defined(BOARD_T5S3_PRO)
  if (!running) return true;
  pausedForDisplay = true;
  shutdownHardware();
  // With the shared rail off, SX1262 DIO1/BUSY can no longer contend with
  // GPIOs the e-paper driver temporarily uses during refresh.
  delay(10);
  return true;
#else
  return true;
#endif
}

bool finishDisplay() {
  if (!active()) return false;
#if defined(BOARD_T5S3_PRO)
  if (!pausedForDisplay) return true;
  pausedForDisplay = false;
  return initializeHardware(&currentConfig);
#else
  return true;
#endif
}

const t5_lora_api_v1 api = {T5_LORA_API_VERSION,
                            sizeof(t5_lora_api_v1),
                            supported,
                            fillDefaultConfig,
                            start,
                            stop,
                            readState,
                            pollPacket,
                            transmit,
                            prepareDisplay,
                            finishDisplay};

}  // namespace

extern "C" const t5_lora_api_v1* t5_lora_get_api(uint32_t version) {
  return version == T5_LORA_API_VERSION && active() ? &api : nullptr;
}
