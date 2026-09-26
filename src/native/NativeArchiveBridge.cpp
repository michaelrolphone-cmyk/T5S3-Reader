#include <T5ArchiveApi.h>
#include <Arduino.h>
#include <HalStorage.h>
#include <ZipFile.h>
#include <Print.h>
#include <cstring>
#include <string>

namespace {
bool mapPath(const char *path, std::string &mapped) {
  if (!path || std::strncmp(path, "/sd/", 4) != 0 || !path[4]) return false;
  mapped.assign(path + 3);
  const char *p = mapped.c_str() + 1;
  while (*p) {
    const char *end = p;
    while (*end && *end != '/') { if (*end == '\\') return false; ++end; }
    const size_t n = static_cast<size_t>(end - p);
    if (!n || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return false;
    if (!*end) break;
    p = end + 1;
    if (!*p) return false;
  }
  return true;
}
bool hasSuffix(const char *value, const char *suffix) {
  if (!value || !suffix || !suffix[0]) return false;
  const size_t n = std::strlen(value), s = std::strlen(suffix);
  if (n < s) return false;
  for (size_t i = 0; i < s; ++i) {
    char a = value[n - s + i], b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
    if (a != b) return false;
  }
  return true;
}
class BoundedOutput final : public Print {
 public:
  BoundedOutput(HalFile &file, uint64_t limit, uint64_t total,
                t5_archive_progress_fn cb, void *ctx)
      : file_(file), limit_(limit), total_(total), cb_(cb), ctx_(ctx), lastReport_(millis()) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t *data, size_t size) override {
    if (failed_ || !data || written_ > limit_ || size > limit_ - written_) { failed_ = true; return 0; }
    const size_t n = file_.write(data, size);
    written_ += n;
    const uint32_t now = millis();
    if (cb_ && (written_ == total_ || written_ - lastBytes_ >= 65536u || now - lastReport_ >= 250u)) {
      cb_(ctx_, written_, total_);
      lastBytes_ = written_;
      lastReport_ = now;
    }
    if (n != size) failed_ = true;
    return n;
  }
  bool good() const { return !failed_ && written_ == total_; }
 private:
  HalFile &file_;
  uint64_t limit_, total_, written_ = 0, lastBytes_ = 0;
  t5_archive_progress_fn cb_;
  void *ctx_;
  uint32_t lastReport_;
  bool failed_ = false;
};
bool findFirst(const char *zipPath, const char *suffix, char *name, size_t cap, uint64_t *size) {
  if (size) *size = 0;
  if (!name || !cap || !size || !Storage.ready()) return false;
  std::string mapped;
  if (!mapPath(zipPath, mapped)) return false;
  std::string path(mapped);
  ZipFile zip(path);
  size_t foundSize = 0;
  if (!zip.findFirstBySuffix(suffix, name, cap, &foundSize)) return false;
  *size = static_cast<uint64_t>(foundSize);
  return true;
}
bool extract(const char *zipPath, const char *entryName, const char *destinationPath,
             uint64_t maxSize, t5_archive_progress_fn progress, void *context) {
  if (!zipPath || !entryName || !destinationPath || !maxSize || !Storage.ready()) return false;
  std::string zipMapped, destMapped;
  if (!mapPath(zipPath, zipMapped) || !mapPath(destinationPath, destMapped)) return false;
  if (Storage.exists(destMapped.c_str())) return false;
  const size_t slash = destMapped.find_last_of('/');
  if (slash != std::string::npos && slash > 0 &&
      !Storage.ensureDirectoryExists(destMapped.substr(0, slash).c_str())) return false;
  std::string archivePath(zipMapped);
  ZipFile zip(archivePath);
  size_t expected = 0;
  if (!zip.getInflatedFileSize(entryName, &expected) || expected == 0 ||
      static_cast<uint64_t>(expected) > maxSize) return false;
  const std::string part = destMapped + ".part";
  if (Storage.exists(part.c_str())) Storage.remove(part.c_str());
  HalFile output = Storage.open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!output || output.isDirectory()) { if (output) output.close(); return false; }
  BoundedOutput sink(output, maxSize, expected, progress, context);
  const bool okay = zip.readFileToStream(entryName, sink, 4096u) && sink.good();
  output.flush();
  output.close();
  if (!okay) { Storage.remove(part.c_str()); return false; }
  if (!Storage.rename(part.c_str(), destMapped.c_str())) {
    Storage.remove(part.c_str());
    return false;
  }
  return true;
}
const t5_archive_api_v1 api = {
  T5_ARCHIVE_API_VERSION, sizeof(t5_archive_api_v1), findFirst, extract
};
}
extern "C" const t5_archive_api_v1 *t5_archive_get_api(uint32_t version) {
  return version == T5_ARCHIVE_API_VERSION ? &api : nullptr;
}
