// Link real native stream/serial bridges; the storage fixture asserts that all
// filesystem callbacks and destructors run with the global stream lock free.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_existing_bridge_fixture
#include "bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop

namespace {
t5_stream_t activeFile = 0, unrelated = 0, replacementBuffer = 0;
unsigned callbacks = 0;
void touchOtherStream(TestStorageOperation op) {
  if (op != TestStorageOperation::Read && op != TestStorageOperation::Write &&
      op != TestStorageOperation::Seek && op != TestStorageOperation::Finish &&
      op != TestStorageOperation::Destroy) return;
  ++callbacks;
  // Would deadlock if an adapter ran with the global stream lock held.
  t5_stream_info_t info{}; info.struct_size = sizeof(info);
  assert(api->info(unrelated, &info) == T5_STREAM_OK);
}
void endDuringRead(TestStorageOperation op) {
  if (op != TestStorageOperation::Read) return;
  testStorageHook = nullptr;
  assert(api->close(activeFile) == T5_STREAM_BUSY);
  nativeStreamsEnd();
  nativeStreamsBegin();
  assert(api->open_buffer(8, &replacementBuffer) == T5_STREAM_OK);
}
void endDuringOpen(TestStorageOperation op) {
  if (op != TestStorageOperation::Open) return;
  testStorageHook = nullptr;
  nativeStreamsEnd();
  nativeStreamsBegin();
}
void rejectConcurrentOperation(TestStorageOperation op) {
  if (op != TestStorageOperation::Read) return;
  testStorageHook = nullptr;
  char value = 0; uint32_t n = 99;
  assert(api->read(activeFile, &value, 1, &n) == T5_STREAM_BUSY && n == 0);
  assert(api->seek(activeFile, 0) == T5_STREAM_BUSY);
  assert(api->finish(activeFile) == T5_STREAM_BUSY);
}
}
int main() {
  files["/direct-input"] = std::make_shared<TestFile>();
  files["/direct-input"]->data = {'a', 'b', 'c'};
  nativeStreamsBegin(); api = t5_stream_get_api(1); assert(api);
  assert(api->open_buffer(8, &unrelated) == T5_STREAM_OK);
  testStorageHook = touchOtherStream;
  assert(api->open_file("/sd/direct-input", T5_STREAM_FILE_READ, &activeFile) == T5_STREAM_OK);
  char bytes[8]{}; uint32_t n = 0;
  assert(api->read(activeFile, bytes, sizeof(bytes), &n) == T5_STREAM_OK && n == 3);
  assert(!std::memcmp(bytes, "abc", 3));
  assert(api->seek(activeFile, 0) == T5_STREAM_OK);
  assert(api->close(activeFile) == T5_STREAM_OK);
  assert(api->open_file("/sd/direct-output", T5_STREAM_FILE_CREATE_NEW, &activeFile) == T5_STREAM_OK);
  assert(api->write(activeFile, "output", 6, &n) == T5_STREAM_OK && n == 6);
  assert(api->finish(activeFile) == T5_STREAM_OK);
  assert(api->close(activeFile) == T5_STREAM_OK);
  assert(callbacks >= 5);
  testStorageHook = nullptr;

  assert(api->open_file("/sd/direct-input", T5_STREAM_FILE_READ, &activeFile) == T5_STREAM_OK);
  testStorageHook = rejectConcurrentOperation;
  assert(api->read(activeFile, bytes, 1, &n) == T5_STREAM_OK && n == 1 && bytes[0] == 'a');
  assert(api->seek(activeFile, 0) == T5_STREAM_OK);
  std::memset(bytes, 'z', sizeof(bytes));
  testStorageHook = endDuringRead;
  assert(api->read(activeFile, bytes, sizeof(bytes), &n) == T5_STREAM_CLOSED && n == 0);
  for (char c : bytes) assert(c == 'z'); // revoked result never reaches app memory
  assert(api->read(replacementBuffer, bytes, sizeof(bytes), &n) == T5_STREAM_AGAIN && n == 0);

  // File opening revalidates its context after SD returns; newly created data
  // is rolled back, while a pre-existing file is preserved on rejected open.
  testStorageHook = endDuringOpen;
  assert(api->open_file("/sd/stale-output", T5_STREAM_FILE_CREATE_NEW, &activeFile) == T5_STREAM_DENIED);
  assert(activeFile == 0 && !files.count("/stale-output"));
  testStorageHook = endDuringOpen;
  assert(api->open_file("/sd/direct-input", T5_STREAM_FILE_READ, &activeFile) == T5_STREAM_DENIED);
  assert(activeFile == 0 && files.at("/direct-input")->data.size() == 3);
  // Context-wide cleanup likewise releases file adapters outside the mutex.
  assert(api->open_file("/sd/direct-input", T5_STREAM_FILE_READ, &activeFile) == T5_STREAM_OK);
  nativeStreamsEnd();
  assert(testStreamMutexDepth == 0);
  std::puts("Direct stream I/O lock, context replacement and retirement tests passed");
}
