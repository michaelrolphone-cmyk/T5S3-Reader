#include <T5AppApi.h>
#include <T5ProgramEspRomApi.h>
#include <T5SerialPortApi.h>
#include <T5StreamApi.h>
#include "runtime/programmer/EspRomProtocol.h"
#include "runtime/programmer/EspRomSession.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_rom_md5.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

namespace {
constexpr uint32_t kMinImage = 0x10000u;
constexpr uint32_t kMaxImage = 0x1000000u;
constexpr uint8_t kFlashBegin = 0x02, kFlashData = 0x03, kFlashEnd = 0x04;
constexpr uint8_t kSync = 0x08, kSpiParams = 0x0b, kSpiAttach = 0x0d;
constexpr uint8_t kFlashMd5 = 0x13, kSecurityInfo = 0x14;
constexpr uint32_t kMaxChunk = T5_STREAM_CHUNK;
std::atomic_flag busy = ATOMIC_FLAG_INIT;

struct Operation {
  const t5_serial_port_api_v1* serial = nullptr;
  const t5_stream_api_v1* streams = nullptr;
  t5_stream_t firmware = 0, rx = 0, tx = 0;
  t5_serial_port_lease_t lease = 0;
  EspRomSession::Watch target;
  t5_program_esp_rom_progress_fn progress = nullptr;
  void* context = nullptr;
  t5_program_esp_rom_status_v1 status{};
  uint32_t lastCallback = 0;
  // Firmware-owned buffers. Allocate the operation on the heap, never on the
  // native app task stack; no whole-image buffer is created.
  uint8_t raw[EspRomProtocol::kCommandBytes]{};
  uint8_t framed[EspRomProtocol::kFramedBytes]{};
  uint8_t block[EspRomProtocol::kBlock]{};
  uint8_t payload[16 + EspRomProtocol::kBlock]{};
  uint8_t reply[EspRomProtocol::kReplyBytes]{};
  size_t replyLength = 0;
  char digest[33]{};

  ~Operation() {
    // The borrowed input stream stays with the app. Releasing the serial lease
    // closes both serial streams and stops the host before the caller returns.
    if (lease && serial) (void)serial->release(lease);
  }
  bool fail(t5_program_esp_rom_result_t code, const char* text) {
    if (status.result != T5_PROGRAM_CANCELLED && status.result != T5_PROGRAM_TARGET_LOST) {
      status.result = code;
      if (text) std::snprintf(status.message, sizeof(status.message), "%s", text);
    }
    return false;
  }
  bool targetLost() {
    status.result = T5_PROGRAM_TARGET_LOST;
    std::snprintf(status.message, sizeof(status.message), "Programming target disconnected");
    return false;
  }
  bool report(uint8_t stage, uint8_t percent, const char* text) {
    status.stage = stage;
    status.percent = percent;
    if (text) std::snprintf(status.message, sizeof(status.message), "%s", text);
    lastCallback = millis();
    // This callback is synchronous and cannot outlive the app invocation.
    if (progress && !progress(context, &status)) {
      status.result = T5_PROGRAM_CANCELLED;
      std::snprintf(status.message, sizeof(status.message), "Programming cancelled");
      return false;
    }
    return true;
  }
  bool tick() {
    esp_task_wdt_reset();
    if (status.result != T5_PROGRAM_OK) return false;
    if (millis() - lastCallback >= 50 && !report(status.stage, status.percent, nullptr)) return false;
    delay(5);
    return status.result == T5_PROGRAM_OK;
  }
  bool transport(bool* ready = nullptr) {
    if (ready) *ready = false;
    if (!serial || !lease) return fail(T5_PROGRAM_IO, "Serial port was not acquired");
    t5_serial_port_state_t state{};
    const auto rc = serial->read_status(lease, &state);
    switch (target.observe(rc, rc == T5_SERIAL_OK ? &state : nullptr)) {
      case EspRomSession::Observation::Lost: return targetLost();
      case EspRomSession::Observation::Error:
        return fail(T5_PROGRAM_IO, "Serial provider stopped or reported an error");
      case EspRomSession::Observation::Ready:
        if (ready) *ready = true;
        return true;
      case EspRomSession::Observation::Waiting: return true;
    }
    return fail(T5_PROGRAM_IO, "Unknown serial provider state");
  }
  bool waitReady(uint32_t timeout) {
    const uint32_t started = millis();
    while (millis() - started < timeout) {
      bool ready = false;
      // One status snapshot per iteration. The guard latches identity while
      // CONFIGURING and detects epoch revocation even before initial READY.
      if (!transport(&ready)) return false;
      if (ready) return true;
      if (!tick()) return false;
    }
    // A detach at the timeout boundary must win over the generic timeout.
    if (!transport()) return false;
    return fail(T5_PROGRAM_TIMEOUT, "Serial device did not become ready");
  }
  bool delayChecked(uint32_t ms) {
    const uint32_t started = millis();
    while (millis() - started < ms) if (!transport() || !tick()) return false;
    return true;
  }
  bool lines(bool dtr, bool rts, uint32_t hold) {
    if (!transport()) return false;
    const auto rc = serial->set_control_lines(lease, dtr, rts);
    if (EspRomSession::Watch::lostControl(rc)) return targetLost();
    if (rc != T5_SERIAL_OK) return fail(T5_PROGRAM_IO, "Target reset control lines failed");
    return waitReady(1500) && delayChecked(hold);
  }
  bool streamRead(t5_stream_t stream, uint8_t* out, size_t length, uint32_t timeout = 5000) {
    const uint32_t started = millis();
    size_t done = 0;
    while (done < length && millis() - started < timeout) {
      uint32_t count = 0;
      const uint32_t requested = static_cast<uint32_t>((length - done) < kMaxChunk ? (length - done) : kMaxChunk);
      const auto rc = streams->read(stream, out + done, requested, &count);
      if (count > requested) return fail(T5_PROGRAM_IO, "Stream returned an invalid byte count");
      // The borrowed firmware source has independent EOF/IO semantics; only
      // a serial stream's terminal result is evidence of target removal.
      if (stream == rx && EspRomSession::Watch::lostStream(rc)) return targetLost();
      if (rc < 0 || (rc == T5_STREAM_EOF && done + count < length))
        return fail(T5_PROGRAM_IO, "Firmware or serial stream terminated");
      done += count;
      esp_task_wdt_reset();
      // Do not sleep for each successful 512-byte SD read. At 16 MiB that
      // would add minutes to hashing alone; progress checks bound cancellation.
      if (!count && !tick()) return false;
    }
    return done == length || fail(T5_PROGRAM_TIMEOUT, "Firmware stream read timed out");
  }
  bool writeAll(const uint8_t* data, size_t length, uint32_t timeout) {
    const uint32_t started = millis();
    size_t done = 0;
    while (done < length && millis() - started < timeout) {
      if (!transport()) return false;
      uint32_t count = 0;
      const uint32_t requested = static_cast<uint32_t>((length - done) < kMaxChunk ? (length - done) : kMaxChunk);
      const auto rc = streams->write(tx, data + done, requested, &count);
      if (EspRomSession::Watch::lostStream(rc)) return targetLost();
      if (count > requested || rc < 0) return fail(T5_PROGRAM_IO, "Serial transmit stream failed");
      done += count;
      if (done != length && !tick()) return false;
    }
    return done == length || fail(T5_PROGRAM_TIMEOUT, "Serial transmit timed out");
  }
  void drain(uint32_t duration) {
    const uint32_t started = millis();
    uint8_t discarded[kMaxChunk];
    do {
      if (rx && transport()) {
        uint32_t n = 0;
        const auto rc = streams->read(rx, discarded, sizeof(discarded), &n);
        if (EspRomSession::Watch::lostStream(rc)) (void)targetLost();
      }
      if (!duration || !tick()) break;
    } while (millis() - started < duration);
  }
  bool receive(uint8_t op, uint32_t timeout) {
    EspRomProtocol::Decoder decoder(sizeof(reply));
    const uint32_t started = millis();
    while (millis() - started < timeout) {
      if (!transport()) return false;
      uint8_t incoming[kMaxChunk];
      uint32_t count = 0;
      const auto rc = streams->read(rx, incoming, sizeof(incoming), &count);
      if (EspRomSession::Watch::lostStream(rc)) return targetLost();
      if (rc < 0) return fail(T5_PROGRAM_IO, "Serial receive stream terminated");
      if (count > sizeof(incoming)) return fail(T5_PROGRAM_IO, "Serial receive overrun");
      for (uint32_t i = 0; i < count; ++i) {
        const auto decoded = decoder.feed(incoming[i]);
        if (decoded == EspRomProtocol::Decoder::Result::Invalid) {
          decoder.reset();
        } else if (decoded == EspRomProtocol::Decoder::Result::Frame) {
          if (decoder.size() >= 2 && decoder.data()[0] == 1 && decoder.data()[1] == op) {
            replyLength = decoder.size();
            std::memcpy(reply, decoder.data(), replyLength);
            return EspRomProtocol::success(op, reply, replyLength,
                                           &status.rom_status, &status.rom_error);
          }
          decoder.reset();
        }
      }
      if (!tick()) return false;
    }
    return false;
  }
  bool command(uint8_t op, const uint8_t* data, size_t length, uint32_t check, uint32_t timeout) {
    status.rom_command = op;
    status.rom_status = status.rom_error = 0xff;
    const size_t framedLength = EspRomProtocol::encode(op, data, length, check,
      raw, sizeof(raw), framed, sizeof(framed));
    if (!framedLength) return fail(T5_PROGRAM_INVALID, "ROM command exceeds bounded packet capacity");
    if (!writeAll(framed, framedLength, timeout)) return false;
    return receive(op, timeout);
  }
  bool hashSource() {
    if (streams->seek(firmware, 0) != T5_STREAM_OK)
      return fail(T5_PROGRAM_IO, "Firmware stream is not seekable");
    md5_context_t md5{};
    esp_rom_md5_init(&md5);
    uint32_t remaining = status.image_bytes;
    unsigned lastPercent = 0;
    while (remaining) {
      const size_t count = remaining < sizeof(block) ? remaining : sizeof(block);
      if (!streamRead(firmware, block, count)) return false;
      esp_rom_md5_update(&md5, block, count);
      remaining -= static_cast<uint32_t>(count);
      const unsigned percent = static_cast<unsigned>((static_cast<uint64_t>(status.image_bytes - remaining) * 100) / status.image_bytes);
      if (percent >= lastPercent + 5 || !remaining) {
        if (!report(T5_PROGRAM_STAGE_HASH, static_cast<uint8_t>(percent), "Calculating source MD5")) return false;
        lastPercent = percent;
      }
    }
    uint8_t bytes[16]{};
    esp_rom_md5_final(bytes, &md5);
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; ++i) { digest[2*i] = hex[bytes[i] >> 4]; digest[2*i+1] = hex[bytes[i] & 15]; }
    digest[32] = 0;
    return streams->seek(firmware, 0) == T5_STREAM_OK ||
           fail(T5_PROGRAM_IO, "Firmware stream rewind failed");
  }
  bool connect() {
    t5_serial_port_request_t request{};
    request.config = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
    const auto rc = serial->acquire(&request, &lease, &rx, &tx);
    if (rc == T5_SERIAL_DISCONNECTED) return targetLost();
    if (rc != T5_SERIAL_OK) return fail(rc == T5_SERIAL_BUSY ? T5_PROGRAM_BUSY : T5_PROGRAM_IO,
                                          "Cannot acquire serial.port for programming");
    if (!waitReady(8000)) return false;
    const uint32_t resetHolds[] = {100u, 250u, 500u};
    uint8_t sync[36];
    std::memset(sync, 0x55, sizeof(sync));
    sync[0] = 7; sync[1] = 7; sync[2] = 0x12; sync[3] = 0x20;
    for (unsigned reset = 0; reset < 3; ++reset) {
      char message[80];
      std::snprintf(message, sizeof(message), "Reset/sync attempt %u/3", reset + 1);
      if (!report(T5_PROGRAM_STAGE_CONNECT, 0, message)) return false;
      drain(0);
      if (!lines(false, true, resetHolds[reset]) || !lines(true, false, 50) ||
          !lines(false, false, 50)) return false;
      for (unsigned attempt = 0; attempt < 5; ++attempt) {
        if (command(kSync, sync, sizeof(sync), 0, 500)) return true;
        if (status.result != T5_PROGRAM_OK) return false;
        drain(25);
      }
    }
    return fail(T5_PROGRAM_TIMEOUT, "SYNC timeout/no ROM reply");
  }
  uint32_t capacity() const {
    uint32_t value = 2u * 1024u * 1024u;
    while (value < status.image_bytes && value < kMaxImage) value <<= 1;
    return value >= status.image_bytes ? value : 0;
  }
  bool configure() {
    if (!report(T5_PROGRAM_STAGE_CONFIGURE, 0, "Checking ROM capabilities")) return false;
    const bool extended = command(kSecurityInfo, nullptr, 0, 0, 750);
    if (status.result != T5_PROGRAM_OK) return false;
    drain(75);
    uint8_t attach[8]{};
    if (!report(T5_PROGRAM_STAGE_CONFIGURE, 0, "Configuring SPI flash")) return false;
    if (!command(kSpiAttach, attach, sizeof(attach), 0, 3000))
      return fail(T5_PROGRAM_IO, "SPI_ATTACH failed");
    uint8_t params[24];
    EspRomProtocol::le32(params, 0);
    EspRomProtocol::le32(params + 4, capacity());
    EspRomProtocol::le32(params + 8, 64u * 1024u);
    EspRomProtocol::le32(params + 12, 4u * 1024u);
    EspRomProtocol::le32(params + 16, 256);
    EspRomProtocol::le32(params + 20, 0xffff);
    if (!command(kSpiParams, params, sizeof(params), 0, 3000))
      return fail(T5_PROGRAM_IO, "SPI_SET_PARAMS failed");
    uint8_t begin[20];
    EspRomProtocol::le32(begin, status.image_bytes);
    EspRomProtocol::le32(begin + 4, (status.image_bytes + EspRomProtocol::kBlock - 1) / EspRomProtocol::kBlock);
    EspRomProtocol::le32(begin + 8, EspRomProtocol::kBlock);
    EspRomProtocol::le32(begin + 12, 0);
    EspRomProtocol::le32(begin + 16, 0);
    const uint32_t megabytes = (status.image_bytes + 0xfffffu) >> 20;
    if (!report(T5_PROGRAM_STAGE_ERASE, 0, "Erasing target flash")) return false;
    if (!command(kFlashBegin, begin, extended ? sizeof(begin) : sizeof(begin) - 4,
                 0, 10000u + megabytes * 40000u))
      return fail(T5_PROGRAM_IO, "FLASH_BEGIN/erase failed");
    return true;
  }
  bool writeFirmware() {
    if (streams->seek(firmware, 0) != T5_STREAM_OK)
      return fail(T5_PROGRAM_IO, "Firmware stream rewind failed");
    const uint32_t blocks = (status.image_bytes + EspRomProtocol::kBlock - 1) / EspRomProtocol::kBlock;
    unsigned lastPercent = 0;
    for (uint32_t sequence = 0; sequence < blocks; ++sequence) {
      const uint32_t remaining = status.image_bytes - status.bytes_written;
      const size_t count = remaining < sizeof(block) ? remaining : sizeof(block);
      if (!streamRead(firmware, block, count)) return false;
      if (count < sizeof(block)) std::memset(block + count, 0xff, sizeof(block) - count);
      EspRomProtocol::le32(payload, EspRomProtocol::kBlock);
      EspRomProtocol::le32(payload + 4, sequence);
      EspRomProtocol::le32(payload + 8, 0);
      EspRomProtocol::le32(payload + 12, 0);
      std::memcpy(payload + 16, block, sizeof(block));
      bool written = false;
      for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (command(kFlashData, payload, sizeof(payload),
                    EspRomProtocol::checksum(block, sizeof(block)), 5000)) { written = true; break; }
        if (status.result != T5_PROGRAM_OK) return false;
      }
      if (!written) {
        char error[100];
        std::snprintf(error, sizeof(error), "FLASH_DATA block %lu failed S%02x E%02x",
                      static_cast<unsigned long>(sequence), status.rom_status, status.rom_error);
        return fail(T5_PROGRAM_IO, error);
      }
      status.bytes_written += static_cast<uint32_t>(count);
      const unsigned percent = static_cast<unsigned>(((sequence + 1) * 100) / blocks);
      if (percent >= lastPercent + 5 || sequence + 1 == blocks) {
        char progressMessage[80];
        std::snprintf(progressMessage, sizeof(progressMessage), "Writing block %lu/%lu",
                      static_cast<unsigned long>(sequence + 1), static_cast<unsigned long>(blocks));
        if (!report(T5_PROGRAM_STAGE_WRITE, static_cast<uint8_t>(percent), progressMessage)) return false;
        lastPercent = percent;
      }
    }
    return true;
  }
  bool verify() {
    if (!report(T5_PROGRAM_STAGE_VERIFY, 0, "Comparing source MD5 with target flash")) return false;
    uint8_t md5[16];
    EspRomProtocol::le32(md5, 0);
    EspRomProtocol::le32(md5 + 4, status.image_bytes);
    EspRomProtocol::le32(md5 + 8, 0);
    EspRomProtocol::le32(md5 + 12, 0);
    const uint32_t megabytes = (status.image_bytes + 0xfffffu) >> 20;
    if (!command(kFlashMd5, md5, sizeof(md5), 0, 5000u + megabytes * 8000u))
      return fail(T5_PROGRAM_IO, "FLASH_MD5 command failed");
    if (replyLength < 44 || EspRomProtocol::le16(reply + 2) < 36 ||
        replyLength < 8u + EspRomProtocol::le16(reply + 2))
      return fail(T5_PROGRAM_IO, "FLASH_MD5 returned malformed digest");
    for (unsigned i = 0; i < 32; ++i) {
      char c = static_cast<char>(reply[8 + i]);
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
      if (c != digest[i]) return fail(T5_PROGRAM_VERIFY_FAILED, "Target MD5 differs from source image");
    }
    return true;
  }
  bool reset() {
    if (!report(T5_PROGRAM_STAGE_RESET, 100, "Resetting target into flashed firmware")) return false;
    uint8_t end[4]{};
    // Some ROMs reset immediately without acknowledging FLASH_END. Preserve
    // the original best-effort command, but require both physical reset steps.
    (void)command(kFlashEnd, end, sizeof(end), 0, 1000);
    if (status.result != T5_PROGRAM_OK) return false;
    if (!lines(false, true, 100)) return false;
    return lines(false, false, 50);
  }
  bool run() {
    if (!report(T5_PROGRAM_STAGE_VALIDATE, 0, "Checking merged firmware image at offset 0")) return false;
    if (status.image_bytes < kMinImage || status.image_bytes > kMaxImage)
      return fail(T5_PROGRAM_INVALID, "Image size must be 64 KiB to 16 MiB");
    t5_stream_info_t info{};
    info.struct_size = sizeof(info);
    if (streams->info(firmware, &info) != T5_STREAM_OK ||
        (info.flags & (T5_STREAM_READ | T5_STREAM_SEEK)) != (T5_STREAM_READ | T5_STREAM_SEEK))
      return fail(T5_PROGRAM_INVALID, "Firmware input must be a readable, seekable stream");
    if (streams->seek(firmware, 0) != T5_STREAM_OK) return fail(T5_PROGRAM_IO, "Cannot seek firmware image");
    uint8_t header[4]{};
    if (!streamRead(firmware, header, sizeof(header))) return false;
    if (header[0] != 0xe9) return fail(T5_PROGRAM_INVALID, "Image is not merged ESP firmware at 0x0");
    if (!report(T5_PROGRAM_STAGE_HASH, 0, "Calculating source MD5") || !hashSource()) return false;
    if (!report(T5_PROGRAM_STAGE_CONNECT, 0, "Powering target and opening serial.port") || !connect()) return false;
    if (!configure() || !writeFirmware() || !verify() || !reset()) return false;
    return report(T5_PROGRAM_STAGE_COMPLETE, 100, "Firmware verified; target reset");
  }
};

t5_program_esp_rom_result_t program(t5_stream_t firmware, uint32_t imageBytes,
    t5_program_esp_rom_progress_fn progress, void* context, t5_program_esp_rom_status_v1* output) {
  if (!output) return T5_PROGRAM_INVALID;
  *output = {};
  output->struct_size = sizeof(*output);
  output->image_bytes = imageBytes;
  output->rom_command = output->rom_status = output->rom_error = 0xff;
  // Immediate failures are also API results: never return BUSY/DENIED while
  // leaving final_status apparently OK with an unset structure version.
  if (!firmware) {
    output->result = T5_PROGRAM_INVALID;
    std::snprintf(output->message, sizeof(output->message), "No firmware input stream");
    return T5_PROGRAM_INVALID;
  }
  if (!t5_app_get_api(T5_APP_ABI_VERSION)) {
    output->result = T5_PROGRAM_DENIED;
    std::snprintf(output->message, sizeof(output->message), "No active application execution context");
    return T5_PROGRAM_DENIED;
  }
  if (busy.test_and_set(std::memory_order_acquire)) {
    output->result = T5_PROGRAM_BUSY;
    std::snprintf(output->message, sizeof(output->message), "ESP ROM programmer already in use");
    return T5_PROGRAM_BUSY;
  }
  // Create the 5+ KiB protocol buffers in bounded heap storage, not on the
  // application's main task stack. Release serial before unlocking busy.
  auto* op = new (std::nothrow) Operation{};
  if (!op) {
    busy.clear(std::memory_order_release);
    output->result = T5_PROGRAM_IO;
    std::snprintf(output->message, sizeof(output->message), "Cannot allocate programmer buffers");
    return T5_PROGRAM_IO;
  }
  op->status.struct_size = sizeof(op->status);
  op->status.image_bytes = imageBytes;
  op->status.rom_command = op->status.rom_status = op->status.rom_error = 0xff;
  op->progress = progress; op->context = context; op->firmware = firmware;
  op->streams = t5_stream_get_api(T5_STREAM_API_VERSION);
  op->serial = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  if (!op->streams || !op->serial || !op->streams->info || !op->streams->seek ||
      !op->streams->read || !op->streams->write || !op->serial->acquire ||
      !op->serial->read_status || !op->serial->set_control_lines || !op->serial->release) {
    op->fail(T5_PROGRAM_UNSUPPORTED, "serial.port or firmware streams unavailable");
  } else if (!op->run() && op->status.result == T5_PROGRAM_OK) {
    op->fail(T5_PROGRAM_IO, "ESP ROM programming failed");
  }
  *output = op->status;
  const auto result = static_cast<t5_program_esp_rom_result_t>(op->status.result);
  delete op;
  busy.clear(std::memory_order_release);
  return result;
}

const t5_program_esp_rom_api_v1 api = {
  T5_PROGRAM_ESP_ROM_API_VERSION, sizeof(t5_program_esp_rom_api_v1),
  T5_PROGRAM_ESP_ROM_CAPABILITY, program
};
} // namespace

extern "C" const t5_program_esp_rom_api_v1* t5_program_esp_rom_get_api(uint32_t version) {
  if (version != T5_PROGRAM_ESP_ROM_API_VERSION || !t5_app_get_api(T5_APP_ABI_VERSION)) return nullptr;
  return &api;
}
