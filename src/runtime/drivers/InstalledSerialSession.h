#pragma once

#include <RiscSerialPortV1.h>
#include <T5SerialPortApi.h>
#include <T5StreamApi.h>
#include <cstdint>

// Hardware-blind lifetime wrapper for one exact installed serial.port@1
// provider generation. The owning InstalledSerialInventory keeps the provider
// ELF and dependencies pinned. This object never discovers transports, parses
// descriptors, selects another provider, or releases the provider graph grant.
namespace RuntimeInstalledProviders {
class InstalledSerialSession final {
 public:
  bool bind(const risc_serial_port_api_v1* api, uint64_t providerDevice,
            uint64_t providerGeneration) {
    if (api_ || token_ || !valid(api) || !providerDevice || !providerGeneration)
      return false;
    api_ = api;
    providerDevice_ = providerDevice;
    providerGeneration_ = providerGeneration;
    return true;
  }

  bool open(const t5_serial_config_t& config) {
    if (!api_ || token_ || started_ || !validConfig(config)) return false;
    const uint64_t opened = api_->open(providerDevice_);
    if (!opened) return false;
    if (!api_->configure(opened, config.baud_rate, config.data_bits,
                         config.parity, config.stop_bits)) {
      // If physical close is uncertain, retain the exact session token so a
      // later checked release retries the SAME provider generation.
      if (!api_->close(opened)) token_ = opened;
      return false;
    }
    token_ = opened;
    config_ = config;
    dtr_ = rts_ = false;
    started_ = true;
    return true;
  }

  bool configure(const t5_serial_config_t& config) {
    if (!started_ || !token_ || !api_ || !validConfig(config)) return false;
    if (!api_->configure(token_, config.baud_rate, config.data_bits,
                         config.parity, config.stop_bits)) return false;
    config_ = config;
    return true;
  }

  bool control(bool dtr, bool rts) {
    if (!started_ || !token_ || !api_ ||
        !api_->control_lines(token_, dtr, rts)) return false;
    dtr_ = dtr;
    rts_ = rts;
    return true;
  }

  int32_t read(uint8_t* dst, uint32_t capacity, uint32_t* out) {
    if (out) *out = 0;
    if (!dst || !capacity) return T5_STREAM_INVALID;
    if (!started_ || !token_ || !api_) return T5_STREAM_CLOSED;
    const int32_t n = api_->read(token_, dst, capacity, 1);
    if (n < 0 || static_cast<uint32_t>(n) > capacity) return T5_STREAM_IO;
    if (out) *out = static_cast<uint32_t>(n);
    return n ? T5_STREAM_OK : T5_STREAM_AGAIN;
  }

  int32_t write(const uint8_t* src, uint32_t length, uint32_t* out) {
    if (out) *out = 0;
    if (!src || !length) return T5_STREAM_INVALID;
    if (!started_ || !token_ || !api_) return T5_STREAM_CLOSED;
    const int32_t n = api_->write(token_, src, length, 1);
    if (n < 0 || static_cast<uint32_t>(n) > length) return T5_STREAM_IO;
    if (out) *out = static_cast<uint32_t>(n);
    return n ? T5_STREAM_OK : T5_STREAM_AGAIN;
  }

  bool closeChecked() {
    if (!token_) { started_ = false; dtr_ = rts_ = false; return true; }
    if (!api_ || !api_->close(token_)) return false;
    token_ = 0;
    started_ = false;
    dtr_ = rts_ = false;
    return true;
  }

  bool unbindChecked() {
    if (!closeChecked()) return false;
    api_ = nullptr;
    providerDevice_ = 0;
    providerGeneration_ = 0;
    config_ = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
    return true;
  }

  bool bound() const { return api_ != nullptr; }
  bool started() const { return started_ && token_ != 0; }
  bool dtr() const { return dtr_; }
  bool rts() const { return rts_; }
  uint64_t token() const { return token_; }
  uint64_t providerDevice() const { return providerDevice_; }
  uint64_t providerGeneration() const { return providerGeneration_; }
  const risc_serial_port_api_v1* api() const { return api_; }
  const t5_serial_config_t& config() const { return config_; }

 private:
  static bool valid(const risc_serial_port_api_v1* api) {
    return api && api->api_version == RISC_SERIAL_PORT_API_V1 &&
           api->struct_size >= sizeof(risc_serial_port_api_v1) &&
           api->open && api->configure && api->control_lines &&
           api->read && api->write && api->close;
  }
  static bool validConfig(const t5_serial_config_t& config) {
    return config.baud_rate >= 300u && config.baud_rate <= 3000000u &&
           config.data_bits >= 5u && config.data_bits <= 8u &&
           config.parity <= T5_SERIAL_PARITY_SPACE &&
           (config.stop_bits == 1u || config.stop_bits == 2u) &&
           config.flow_control == T5_SERIAL_FLOW_NONE;
  }

  const risc_serial_port_api_v1* api_ = nullptr;
  uint64_t providerDevice_ = 0;
  uint64_t providerGeneration_ = 0;
  uint64_t token_ = 0;
  t5_serial_config_t config_{115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
  bool dtr_ = false;
  bool rts_ = false;
  bool started_ = false;
};
} // namespace RuntimeInstalledProviders
