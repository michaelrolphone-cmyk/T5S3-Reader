#include "runtime/streams/HttpStreamTransfer.h"
#include "runtime/streams/HttpUrlValidation.h"
#include "runtime/streams/StreamRuntime.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace RuntimeHttpStreams;
using namespace RuntimeStreams;
namespace {
constexpr uint32_t owner = 42;
struct Mock {
  Registry registry;
  t5_stream_api_v1 api{};
  std::vector<uint8_t> body;
  std::vector<uint8_t> staged;
  size_t position = 0;
  size_t failReadAt = SIZE_MAX;
  size_t failWriteAt = SIZE_MAX;
  uint32_t now = 0;
  unsigned slowWrites = 0;
  unsigned sourceClose = 0, fileClose = 0, fileFinish = 0;
  bool stageExists = false, finishFails = false, cancelled = false, stall = false;
  int32_t httpOpenResult = T5_STREAM_OK;
  t5_stream_t capturedSource = 0, capturedFile = 0;
  static Mock* active;

  explicit Mock(size_t size) : body(size) {
    active = this;
    for (size_t i = 0; i < body.size(); ++i) body[i] = static_cast<uint8_t>(i % 251);
    api.api_version = T5_STREAM_API_VERSION;
    api.struct_size = sizeof(api);
    api.open_http = [](const char* url, t5_stream_t* out) -> int32_t {
      if (active->httpOpenResult != T5_STREAM_OK) return active->httpOpenResult;
      if (!url || std::strcmp(url, "https://example.test/payload")) return T5_STREAM_INVALID;
      Provider provider{active, readSource, nullptr, nullptr, nullptr, closeSource};
      const int32_t result = active->registry.attach(owner, T5_STREAM_BYTES, T5_STREAM_READ, provider, out);
      if (result == T5_STREAM_OK) active->capturedSource = *out;
      return result;
    };
    api.open_file = [](const char* path, uint32_t mode, t5_stream_t* out) -> int32_t {
      if (!path || std::strcmp(path, "/sd/Apps/test.elf.part") ||
          mode != T5_STREAM_FILE_CREATE_NEW || active->stageExists) return T5_STREAM_IO;
      active->stageExists = true;
      Provider provider{active, nullptr, writeFile, nullptr, finishFile, closeFile};
      const int32_t result = active->registry.attach(owner, T5_STREAM_BYTES, T5_STREAM_WRITE, provider, out);
      if (result == T5_STREAM_OK) active->capturedFile = *out;
      return result;
    };
    api.read = [](t5_stream_t handle, void* data, uint32_t size, uint32_t* count) -> int32_t {
      return active->registry.read(owner, handle, data, size, count);
    };
    api.finish = [](t5_stream_t handle) -> int32_t { return active->registry.finish(owner, handle); };
    api.close = [](t5_stream_t handle) -> int32_t { return active->registry.close(owner, handle); };
    api.pipe_connect = [](t5_stream_t source, t5_stream_t dest, uint32_t policy, t5_pipe_t* out) -> int32_t {
      return active->registry.connect(owner, source, dest, policy, out);
    };
    api.pipe_cancel = [](t5_pipe_t pipe) -> int32_t { return active->registry.cancel(owner, pipe); };
    api.pipe_close = [](t5_pipe_t pipe) -> int32_t { return active->registry.closePipe(owner, pipe); };
    api.pipe_info = [](t5_pipe_t pipe, t5_pipe_info_t* out) -> int32_t {
      return active->registry.pipeInfo(owner, pipe, out);
    };
  }

  static int32_t readSource(void* ctx, void* data, uint32_t size, uint32_t* count) {
    auto& mock = *static_cast<Mock*>(ctx);
    if (mock.stall) return T5_STREAM_AGAIN;
    if (mock.position >= mock.failReadAt) return T5_STREAM_IO;
    if (mock.position == mock.body.size()) return T5_STREAM_EOF;
    *count = static_cast<uint32_t>(std::min<size_t>({size, mock.body.size() - mock.position,
                                                     mock.failReadAt - mock.position}));
    std::memcpy(data, mock.body.data() + mock.position, *count);
    mock.position += *count;
    return T5_STREAM_OK;
  }
  static int32_t writeFile(void* ctx, const void* data, uint32_t size, uint32_t* count) {
    auto& mock = *static_cast<Mock*>(ctx);
    if (mock.staged.size() >= mock.failWriteAt) return T5_STREAM_IO;
    // An intentionally slow but lossless sink, including occasional stalls.
    if (++mock.slowWrites % 7 == 0) return T5_STREAM_AGAIN;
    *count = static_cast<uint32_t>(std::min<size_t>({size, 17, mock.failWriteAt - mock.staged.size()}));
    const auto* bytes = static_cast<const uint8_t*>(data);
    mock.staged.insert(mock.staged.end(), bytes, bytes + *count);
    return T5_STREAM_OK;
  }
  static int32_t finishFile(void* ctx) {
    auto& mock = *static_cast<Mock*>(ctx);
    ++mock.fileFinish;
    return mock.finishFails ? T5_STREAM_IO : T5_STREAM_OK;
  }
  static void closeFile(void* ctx) { ++static_cast<Mock*>(ctx)->fileClose; }
  static void closeSource(void* ctx) { ++static_cast<Mock*>(ctx)->sourceClose; }
  static uint32_t clock(void* ctx) { return static_cast<Mock*>(ctx)->now; }
  static void idle(void* ctx) {
    auto& mock = *static_cast<Mock*>(ctx);
    mock.now += 10;
    mock.registry.pump();
  }
  static bool isCancelled(void* ctx) { return static_cast<Mock*>(ctx)->cancelled; }
  Hooks hooks(uint32_t timeout = 60000) { return {this, clock, idle, isCancelled, timeout}; }
};
Mock* Mock::active = nullptr;

bool collect(void* destination, const uint8_t* bytes, uint32_t count) {
  auto& out = *static_cast<std::vector<uint8_t>*>(destination);
  out.insert(out.end(), bytes, bytes + count);
  return true;
}
void onProgress(void* destination, uint64_t count) {
  *static_cast<uint64_t*>(destination) = count;
}
void assertReleased(Mock& mock) {
  t5_stream_info_t info{};
  info.struct_size = sizeof(info);
  if (mock.capturedSource) assert(mock.registry.info(owner, mock.capturedSource, &info) == T5_STREAM_INVALID);
  if (mock.capturedFile) assert(mock.registry.info(owner, mock.capturedFile, &info) == T5_STREAM_INVALID);
  assert(mock.sourceClose == (mock.capturedSource != 0));
  assert(mock.fileClose == (mock.capturedFile != 0));
  mock.registry.release(owner);
}
} // namespace

int main() {
  {
    size_t length = 0;
    assert(RuntimeHttpUrl::validate("https://example.test/app.elf", 1024, &length) ==
           RuntimeHttpUrl::Status::Valid);
    assert(length == std::strlen("https://example.test/app.elf"));
    assert(RuntimeHttpUrl::validate("http://example.test/app.elf", 1024) ==
           RuntimeHttpUrl::Status::Valid);
    assert(RuntimeHttpUrl::validate(nullptr, 1024) == RuntimeHttpUrl::Status::Null);
    assert(RuntimeHttpUrl::validate("ftp://example.test/app.elf", 1024) ==
           RuntimeHttpUrl::Status::UnsupportedScheme);
    const std::string tooLong(1024, 'x');
    assert(RuntimeHttpUrl::validate(tooLong.c_str(), 1024, &length) ==
           RuntimeHttpUrl::Status::TooLong);
    assert(length == 1024);
  }
  {
    Mock mock(10000);
    std::vector<uint8_t> output;
    uint64_t total = 0;
    assert(fetch(&mock.api, "https://example.test/payload", mock.hooks(), collect, &output,
                 10000, &total) == Result::Ok);
    assert(total == mock.body.size() && output == mock.body);
    assertReleased(mock);
  }
  {
    Mock mock(10000);
    std::vector<uint8_t> output;
    assert(fetch(&mock.api, "https://example.test/payload", mock.hooks(), collect, &output,
                 9999) == Result::Transfer);
    assertReleased(mock);
  }
  {
    Mock mock(10000);
    mock.failReadAt = 2048;
    std::vector<uint8_t> output;
    assert(fetch(&mock.api, "https://example.test/payload", mock.hooks(), collect, &output) == Result::Http);
    assertReleased(mock);
  }
  {
    Mock mock(10000);
    uint64_t progress = 0, total = 0;
    bool created = false;
    assert(download(&mock.api, "https://example.test/payload", "/sd/Apps/test.elf.part",
                    mock.hooks(), onProgress, &progress, &total, &created) == Result::Ok);
    assert(created && total == 10000 && progress == 10000 && mock.staged == mock.body);
    assert(mock.fileFinish == 1 && mock.slowWrites > 7);
    assertReleased(mock);
  }
  {
    Mock mock(100);
    // The exclusive open loses the race to an existing stage: the caller must
    // leave it untouched, even if its earlier exists() check saw no file.
    mock.stageExists = true;
    mock.staged = {0x42, 0x13, 0x99};
    const std::vector<uint8_t> sentinel = mock.staged;
    bool created = true;
    uint64_t total = 123;
    assert(download(&mock.api, "https://example.test/payload", "/sd/Apps/test.elf.part",
                    mock.hooks(), nullptr, nullptr, &total, &created) == Result::File);
    assert(!created && total == 0 && mock.staged == sentinel);
    assert(mock.sourceClose == 0 && mock.fileClose == 0);
  }
  {
    Mock mock(1000);
    mock.httpOpenResult = T5_STREAM_LIMIT;
    bool created = false;
    int32_t openStatus = T5_STREAM_OK;
    assert(download(&mock.api, "https://example.test/payload", "/sd/Apps/test.elf.part",
                    mock.hooks(), nullptr, nullptr, nullptr, &created, &openStatus) == Result::Http);
    assert(created && openStatus == T5_STREAM_LIMIT);
    assert(mock.sourceClose == 0 && mock.fileClose == 1);
    mock.registry.release(owner);
  }
  {
    Mock mock(10000);
    mock.failReadAt = 2048;
    bool created = false;
    assert(download(&mock.api, "https://example.test/payload", "/sd/Apps/test.elf.part",
                    mock.hooks(), nullptr, nullptr, nullptr, &created) == Result::Transfer);
    assert(created && mock.fileFinish == 0);
    assertReleased(mock);
  }
  {
    Mock mock(10000);
    mock.failWriteAt = 2048;
    assert(download(&mock.api, "https://example.test/payload", "/sd/Apps/test.elf.part",
                    mock.hooks()) == Result::Transfer);
    assert(mock.fileFinish == 0);
    assertReleased(mock);
  }
  {
    Mock mock(1000);
    mock.finishFails = true;
    assert(download(&mock.api, "https://example.test/payload", "/sd/Apps/test.elf.part",
                    mock.hooks()) == Result::File);
    assert(mock.fileFinish == 1);
    assertReleased(mock);
  }
  {
    Mock mock(1000);
    mock.stall = true;
    assert(download(&mock.api, "https://example.test/payload", "/sd/Apps/test.elf.part",
                    mock.hooks(100)) == Result::Timeout);
    assert(mock.fileFinish == 0);
    assertReleased(mock);
  }
  {
    Mock mock(1000);
    mock.cancelled = true;
    assert(download(&mock.api, "https://example.test/payload", "/sd/Apps/test.elf.part",
                    mock.hooks()) == Result::Cancelled);
    assert(mock.fileFinish == 0);
    assertReleased(mock);
  }
  std::cout << "HTTP stream transfer tests passed, including exclusive staged-file ownership\n";
}
