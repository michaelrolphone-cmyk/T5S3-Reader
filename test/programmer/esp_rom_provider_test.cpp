// Exercise the actual runtime provider with deterministic serial/stream faults.
// MD5 is stubbed to a constant digest: this tests lifecycle and protocol flow,
// not the ESP-IDF cryptographic implementation or physical USB signaling.
#include <T5AppApi.h>
#include <T5ProgramEspRomApi.h>
#include <T5SerialPortApi.h>
#include <T5StreamApi.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

namespace {
enum class Fault { None, CancelHash, ShortSource, LostSync, LostErase, LostWrite,
                   ResetControlIO, ResetControlLost, Md5Mismatch, NoFlashEndReply, Reentrant };
struct Fixture {
  Fault fault = Fault::None;
  std::vector<uint8_t> image;
  std::deque<uint8_t> replies;
  std::vector<uint8_t> frame;
  uint32_t pos = 0;
  int acquired = 0, released = 0, controlCalls = 0, flashBlocks = 0;
  bool leaseLive = false, connected = true, escaped = false;
  bool flashEndSeen = false, completeReported = false, nestedChecked = false;
} fixture;
uint32_t fakeClock = 1;
bool allowApp = true;

void enqueue(uint8_t op) {
  const bool md5 = op == 0x13;
  fixture.replies.push_back(0xc0);
  fixture.replies.push_back(1);
  fixture.replies.push_back(op);
  fixture.replies.push_back(md5 ? 36 : 4);
  fixture.replies.push_back(0);
  for (int i = 0; i < 4; ++i) fixture.replies.push_back(0);
  if (md5) {
    for (int i = 0; i < 32; ++i)
      fixture.replies.push_back(fixture.fault == Fault::Md5Mismatch ? 'f' : '0');
  }
  for (int i = 0; i < 4; ++i) fixture.replies.push_back(0);
  fixture.replies.push_back(0xc0);
}
void completedFrame() {
  if (fixture.frame.size() < 8) return;
  const uint8_t op = fixture.frame[1];
  if (op == 0x03) ++fixture.flashBlocks;
  if (op == 0x04) fixture.flashEndSeen = true;
  if ((fixture.fault == Fault::LostSync && op == 0x08) ||
      (fixture.fault == Fault::LostErase && op == 0x02) ||
      (fixture.fault == Fault::LostWrite && op == 0x03)) {
    fixture.connected = false;
    return;
  }
  if (fixture.fault != Fault::NoFlashEndReply || op != 0x04) enqueue(op);
}
t5_serial_result_t acquire(const t5_serial_port_request_t* request,
                           t5_serial_port_lease_t* lease, t5_stream_t* rx, t5_stream_t* tx) {
  assert(request && lease && rx && tx && !fixture.leaseLive);
  fixture.leaseLive = true;
  ++fixture.acquired;
  *lease = 10; *rx = 20; *tx = 30;
  return T5_SERIAL_OK;
}
t5_serial_result_t readStatus(t5_serial_port_lease_t lease, t5_serial_port_state_t* out) {
  assert(lease == 10 && fixture.leaseLive && out);
  if (!fixture.connected) return T5_SERIAL_DISCONNECTED;
  *out = {};
  out->status = T5_SERIAL_STATUS_READY;
  out->connected = 1;
  out->device = 77;
  return T5_SERIAL_OK;
}
t5_serial_result_t control(t5_serial_port_lease_t lease, bool, bool) {
  assert(lease == 10 && fixture.leaseLive);
  ++fixture.controlCalls;
  if (!fixture.connected) return T5_SERIAL_DISCONNECTED;
  if (fixture.flashEndSeen && fixture.fault == Fault::ResetControlIO) return T5_SERIAL_IO;
  if (fixture.flashEndSeen && fixture.fault == Fault::ResetControlLost) {
    fixture.connected = false;
    return T5_SERIAL_DISCONNECTED;
  }
  return T5_SERIAL_OK;
}
t5_serial_result_t release(t5_serial_port_lease_t lease) {
  assert(lease == 10 && fixture.leaseLive);
  fixture.leaseLive = false;
  ++fixture.released;
  return T5_SERIAL_OK;
}
t5_stream_result_t streamInfo(t5_stream_t stream, t5_stream_info_t* out) {
  assert(stream == 1 && out);
  *out = {};
  out->struct_size = sizeof(*out);
  out->flags = T5_STREAM_READ | T5_STREAM_SEEK;
  return T5_STREAM_OK;
}
t5_stream_result_t streamSeek(t5_stream_t stream, uint64_t offset) {
  assert(stream == 1);
  if (offset > fixture.image.size()) return T5_STREAM_IO;
  fixture.pos = static_cast<uint32_t>(offset);
  return T5_STREAM_OK;
}
t5_stream_result_t streamRead(t5_stream_t stream, void* data, uint32_t capacity, uint32_t* count) {
  assert(count && data && capacity <= T5_STREAM_CHUNK);
  *count = 0;
  if (stream == 1) {
    const size_t available = fixture.image.size() - fixture.pos;
    const size_t size = std::min(static_cast<size_t>(capacity), available);
    if (size) {
      std::copy_n(fixture.image.data() + fixture.pos, size, static_cast<uint8_t*>(data));
      fixture.pos += static_cast<uint32_t>(size);
      *count = static_cast<uint32_t>(size);
      return T5_STREAM_OK;
    }
    return T5_STREAM_EOF;
  }
  assert(stream == 20 && fixture.leaseLive);
  if (!fixture.connected) return T5_STREAM_DISCONNECTED;
  auto* dest = static_cast<uint8_t*>(data);
  while (*count < capacity && !fixture.replies.empty()) {
    dest[(*count)++] = fixture.replies.front();
    fixture.replies.pop_front();
  }
  return *count ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
t5_stream_result_t streamWrite(t5_stream_t stream, const void* data,
                               uint32_t size, uint32_t* count) {
  assert(stream == 30 && fixture.leaseLive && data && count && size <= T5_STREAM_CHUNK);
  *count = 0;
  if (!fixture.connected) return T5_STREAM_DISCONNECTED;
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (uint32_t i = 0; i < size; ++i) {
    const uint8_t value = bytes[i];
    if (value == 0xc0) {
      completedFrame();
      fixture.frame.clear();
      fixture.escaped = false;
    } else if (fixture.escaped) {
      fixture.frame.push_back(value == 0xdc ? 0xc0 : 0xdb);
      fixture.escaped = false;
    } else if (value == 0xdb) {
      fixture.escaped = true;
    } else {
      fixture.frame.push_back(value);
    }
  }
  *count = size;
  return T5_STREAM_OK;
}
bool progress(void*, const t5_program_esp_rom_status_v1* status) {
  if (status->stage == T5_PROGRAM_STAGE_COMPLETE) fixture.completeReported = true;
  if (fixture.fault == Fault::Reentrant && !fixture.nestedChecked) {
    fixture.nestedChecked = true;
    auto* programmer = t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION);
    assert(programmer);
    t5_program_esp_rom_status_v1 nested{};
    assert(programmer->program(1, 0x10000u, nullptr, nullptr, &nested) == T5_PROGRAM_BUSY);
    assert(nested.result == T5_PROGRAM_BUSY && nested.struct_size == sizeof(nested));
    assert(nested.message[0] && fixture.acquired == 0);
  }
  return !(fixture.fault == Fault::CancelHash &&
           status->stage == T5_PROGRAM_STAGE_HASH && status->percent >= 5);
}
void resetFixture(Fault fault) {
  fixture = Fixture{};
  fixture.fault = fault;
  fixture.image.assign(0x10000u, 0x55);
  fixture.image[0] = 0xe9;
  if (fault == Fault::ShortSource) fixture.image.resize(1024);
  fakeClock = 1;
}
t5_program_esp_rom_status_v1 run(Fault fault, t5_program_esp_rom_result_t expected) {
  resetFixture(fault);
  const auto* programmer = t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION);
  assert(programmer && programmer->program);
  t5_program_esp_rom_status_v1 result{};
  const auto rc = programmer->program(1, 0x10000u, progress, nullptr, &result);
  assert(rc == expected && result.result == expected);
  assert(result.struct_size == sizeof(result));
  assert(!fixture.leaseLive && fixture.acquired == fixture.released);
  assert(fixture.completeReported == (expected == T5_PROGRAM_OK));
  return result;
}
}  // namespace

uint32_t millis() { return fakeClock; }
void delay(uint32_t ms) { fakeClock += ms; }
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  static const t5_app_api_v1 api{};
  return allowApp && version == T5_APP_ABI_VERSION ? &api : nullptr;
}
extern "C" const t5_serial_port_api_v1* t5_serial_port_get_api(uint32_t version) {
  static t5_serial_port_api_v1 api{};
  api.api_version = T5_SERIAL_PORT_API_VERSION;
  api.struct_size = sizeof(api);
  api.capability_id = T5_SERIAL_PORT_CAPABILITY;
  api.acquire = acquire;
  api.read_status = readStatus;
  api.set_control_lines = control;
  api.release = release;
  return version == T5_SERIAL_PORT_API_VERSION ? &api : nullptr;
}
extern "C" const t5_stream_api_v1* t5_stream_get_api(uint32_t version) {
  static t5_stream_api_v1 api{};
  api.api_version = T5_STREAM_API_VERSION;
  api.struct_size = sizeof(api);
  api.info = streamInfo;
  api.seek = streamSeek;
  api.read = streamRead;
  api.write = streamWrite;
  return version == T5_STREAM_API_VERSION ? &api : nullptr;
}

int main() {
  auto success = run(Fault::None, T5_PROGRAM_OK);
  assert(success.bytes_written == 0x10000u && fixture.flashBlocks == 64);
  assert(fixture.flashEndSeen && fixture.controlCalls == 5);
  auto cancelled = run(Fault::CancelHash, T5_PROGRAM_CANCELLED);
  assert(cancelled.bytes_written == 0 && fixture.acquired == 0 && fixture.released == 0);
  auto source = run(Fault::ShortSource, T5_PROGRAM_IO);
  assert(source.bytes_written == 0 && fixture.acquired == 0);
  auto sync = run(Fault::LostSync, T5_PROGRAM_TARGET_LOST);
  assert(sync.bytes_written == 0 && fixture.released == 1);
  auto erase = run(Fault::LostErase, T5_PROGRAM_TARGET_LOST);
  assert(erase.bytes_written == 0 && fixture.released == 1);
  auto write = run(Fault::LostWrite, T5_PROGRAM_TARGET_LOST);
  assert(write.bytes_written == 0 && fixture.flashBlocks == 1 && fixture.released == 1);
  auto resetIO = run(Fault::ResetControlIO, T5_PROGRAM_IO);
  assert(resetIO.bytes_written == 0x10000u && fixture.flashEndSeen && fixture.released == 1);
  auto resetLost = run(Fault::ResetControlLost, T5_PROGRAM_TARGET_LOST);
  assert(resetLost.bytes_written == 0x10000u && fixture.released == 1);
  auto mismatch = run(Fault::Md5Mismatch, T5_PROGRAM_VERIFY_FAILED);
  assert(mismatch.bytes_written == 0x10000u && !fixture.flashEndSeen);
  // Some ROMs reset without a FLASH_END acknowledgment; hardware reset lines
  // remain the final action for this backward-compatible implementation.
  auto noAck = run(Fault::NoFlashEndReply, T5_PROGRAM_OK);
  assert(noAck.bytes_written == 0x10000u && fixture.controlCalls == 5);
  (void)run(Fault::Reentrant, T5_PROGRAM_OK);
  assert(fixture.nestedChecked && fixture.acquired == 1);
  resetFixture(Fault::None);
  const auto* programmer = t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION);
  t5_program_esp_rom_status_v1 immediate{};
  assert(programmer->program(0, 0x10000u, nullptr, nullptr, &immediate) == T5_PROGRAM_INVALID);
  assert(immediate.result == T5_PROGRAM_INVALID && immediate.struct_size == sizeof(immediate));
  allowApp = false;
  assert(programmer->program(1, 0x10000u, nullptr, nullptr, &immediate) == T5_PROGRAM_DENIED);
  assert(immediate.result == T5_PROGRAM_DENIED && immediate.struct_size == sizeof(immediate));
  allowApp = true;
  assert(fixture.acquired == 0 && fixture.released == 0);
  std::puts("ESP ROM provider fault-injection tests passed");
}
