// Real stream bridge, with storage asserting no I/O under its global mutex.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_existing_bridge_fixture
#include "bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include <T5PackageResourceApi.h>
#include "runtime/packages/PackageUseGate.h"
using namespace RuntimePackages;
namespace {
void put(const char* path, const std::string& data) {
  auto f = std::make_shared<TestFile>();
  f->data.assign(data.begin(), data.end()); files[path] = f;
}
std::string manifest(const char* version = "1.0.0") {
  const std::string sha(64, '0');
  return std::string("{\"schema\":2,\"kind\":\"application\",\"id\":\"example\",\"version\":\"") + version +
      "\",\"artifact\":\"example.elf\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":1,\"entries\":["
      "{\"name\":\"example.elf\",\"size_bytes\":52,\"sha256\":\"" + sha + "\",\"executable\":true},"
      "{\"name\":\"assets/text.txt\",\"size_bytes\":3,\"sha256\":\"" + sha + "\",\"executable\":false}],\"requires\":[]}";
}
void replaceContext(TestStorageOperation op) {
  if (op != TestStorageOperation::Open) return;
  testStorageHook = nullptr; nativeStreamsEnd(); nativeStreamsBegin();
}
}
int main() {
  Identity identity{}; assert(makeIdentity(Kind::Application, "example", "1.0.0", "example.elf", false, &identity));
  put("/Apps/example/.package.json", manifest()); put("/Apps/example/assets/text.txt", "abc");
  nativeStreamsBegin(); api = t5_stream_get_api(1); assert(api);
  assert(!t5_package_resource_get_api(1)); // legacy/unauthorized root
  assert(!nativeStreamsBindPackageResources(identity)); // loader must own its pin
  auto& gate = systemPackageUseGate(); assert(gate.pin("/Apps/example"));
  assert(nativeStreamsBindPackageResources(identity));
  assert(!nativeStreamsBindPackageResources(identity));
  const auto* resources = t5_package_resource_get_api(1); assert(resources);
  assert(!t5_package_resource_get_api(2));
  t5_stream_t h = 99;
  assert(resources->open("../other/data", &h) == T5_STREAM_INVALID && !h);
  assert(resources->open("example.elf", &h) == T5_STREAM_DENIED && !h);
  assert(resources->open("assets/unknown.txt", &h) == T5_STREAM_DENIED && !h);
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_OK && h);
  assert(gate.unpin("/Apps/example")); // stream itself retains exact generation
  assert(!gate.beginReplacement("/Apps/example"));
  char data[4]{}; uint32_t n = 99;
  assert(api->write(h, "z", 1, &n) == T5_STREAM_DENIED && !n);
  assert(api->read(h, data, sizeof(data), &n) == T5_STREAM_OK && n == 3 && !std::strcmp(data, "abc"));
  assert(api->seek(h, 0) == T5_STREAM_OK);
  assert(api->close(h) == T5_STREAM_OK);
  assert(gate.beginReplacement("/Apps/example")); assert(gate.endReplacement("/Apps/example"));
  put("/Apps/example/.package.json", manifest("2.0.0"));
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_IO && !h);
  put("/Apps/example/.package.json", manifest());
  put("/Apps/example/assets/text.txt", "changed length");
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_IO && !h);
  put("/Apps/example/assets/text.txt", "abc");
  testStorageHook = replaceContext;
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_DENIED && !h);
  assert(!gate.pinned("/Apps/example")); assert(!t5_package_resource_get_api(1));
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_DENIED && !h);
  assert(gate.pin("/Apps/example")); assert(nativeStreamsBindPackageResources(identity));
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_OK);
  assert(gate.unpin("/Apps/example")); nativeStreamsEnd();
  assert(!gate.pinned("/Apps/example"));
  nativeStreamsBegin(); assert(gate.pin("/Apps/example")); assert(nativeStreamsBindPackageResources(identity));
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_OK);
  assert(gate.unpin("/Apps/example")); closeOk = false;
  assert(api->finish(h) == T5_STREAM_IO); assert(api->close(h) == T5_STREAM_OK);
  assert(gate.pinned("/Apps/example")); // uncertain close must not permit replacement
  closeOk = true; nativeStreamsEnd();
  std::puts("Installed resource stream scope, pins, identity, revocation and uncertain close PASS");
}
