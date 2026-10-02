#include <assert.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "HalStorage.h"
#include "T5StorageApi.h"
#include "freertos/task.h"

HalStorage native_storage_test_storage;
TickType_t native_storage_test_ticks;
size_t native_storage_test_yield_count;

#include "../src/native/NativePlatformBridge.cpp"

static std::string bytes(size_t size) {
  std::string result(size, '\0');
  for (size_t i = 0; i < size; ++i) result[i] = static_cast<char>((i * 37u + 11u) & 0xFFu);
  return result;
}

int main() {
  const t5_storage_api_v1* api = t5_storage_get_api(T5_STORAGE_API_VERSION);
  assert(api && api->read_file);

  const std::string payload = bytes(60017);
  auto& file = native_storage_test_storage.addFile("/large.bin", payload, 1021);

  /* A size probe must report the full file length, not the legacy 50,000 cap. */
  size_t size = 0;
  const int reads_before_probe = file.read_calls;
  assert(api->read_file("/sd/large.bin", nullptr, 0, &size));
  assert(size == payload.size());
  assert(file.read_calls == reads_before_probe);

  auto& empty_file = native_storage_test_storage.addFile("/empty.bin", "");
  size = 1;
  assert(api->read_file("/sd/empty.bin", nullptr, 0, &size));
  assert(size == 0);
  assert(empty_file.close_calls == 1);

  /* Short successful SD reads must be accumulated until the advertised size. */
  std::vector<char> output(payload.size());
  size = 0;
  const size_t yields_before_read = native_storage_test_yield_count;
  assert(api->read_file("/sd/large.bin", output.data(), output.size(), &size));
  assert(size == payload.size());
  assert(std::memcmp(output.data(), payload.data(), payload.size()) == 0);
  assert(file.read_calls > 0);
  assert(file.close_calls >= 2);
  assert(native_storage_test_yield_count > yields_before_read);

  /* The API reports the required size while refusing a buffer that is too small. */
  size = 0;
  const int reads_before_small_buffer = file.read_calls;
  assert(!api->read_file("/sd/large.bin", output.data(), output.size() - 1, &size));
  assert(size == payload.size());
  assert(file.read_calls == reads_before_small_buffer);

  file.close_allowed = false;
  size = 0;
  assert(!api->read_file("/sd/large.bin", nullptr, 0, &size));
  assert(size == payload.size());
  file.close_allowed = true;

  /* An I/O error after partial progress fails; a fresh call can retry completely. */
  file.fail_read_call = file.read_calls + 2;
  size = 0;
  std::fill(output.begin(), output.end(), static_cast<char>(0xA5));
  assert(!api->read_file("/sd/large.bin", output.data(), output.size(), &size));
  assert(size == payload.size());
  file.fail_read_call = -1;
  size = 0;
  assert(api->read_file("/sd/large.bin", output.data(), output.size(), &size));
  assert(size == payload.size());
  assert(std::memcmp(output.data(), payload.data(), payload.size()) == 0);

  size = 123;
  assert(!api->read_file("/sd/missing.bin", output.data(), output.size(), &size));
  assert(size == 0);
  auto& unreadable = native_storage_test_storage.addFile("/unreadable.bin", "data");
  unreadable.open_allowed = false;
  size = 123;
  assert(!api->read_file("/sd/unreadable.bin", output.data(), output.size(), &size));
  assert(size == 0);
  size = 123;
  assert(!api->read_file("/sd/../large.bin", output.data(), output.size(), &size));
  assert(size == 0);

  native_storage_test_storage.setReady(false);
  size = 123;
  assert(!api->read_file("/sd/large.bin", output.data(), output.size(), &size));
  assert(size == 0);
  return 0;
}
