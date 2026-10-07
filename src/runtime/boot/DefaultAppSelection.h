#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <HalStorage.h>
#include <AppManifestRules.h>
#include <fcntl.h>

// A saved default is an ELF artifact basename, never a pathname or a grant.
// The ordinary installed-app resolver verifies its package/sidecar before use.
namespace RuntimeDefaultApp {
enum class Selection : uint8_t { Absent, Invalid, Ready };
constexpr const char* kPath = "/RiscRTE/default-app.txt";
constexpr size_t kMaxBytes = 96;
inline Selection parse(const char* bytes, size_t length, char (&artifact)[96]) {
  artifact[0] = 0;
  if (!bytes || !length || length > kMaxBytes) return Selection::Invalid;
  if (bytes[length - 1] == '\n') --length;
  if (length && bytes[length - 1] == '\r') --length;
  if (!length || length >= sizeof(artifact)) return Selection::Invalid;
  for (size_t i = 0; i < length; ++i)
    if (bytes[i] < '!' || bytes[i] > '~' || bytes[i] == '/' || bytes[i] == '\\')
      return Selection::Invalid;
  std::memcpy(artifact, bytes, length);
  artifact[length] = 0;
  if (!t5_safe_elf_name(artifact)) { artifact[0] = 0; return Selection::Invalid; }
  return Selection::Ready;
}
inline Selection read(char (&artifact)[96]) {
  artifact[0] = 0;
  if (!Storage.ready()) return Selection::Invalid;
  auto file = Storage.open(kPath, O_RDONLY);
  if (!file.isOpen()) return Storage.exists(kPath) ? Selection::Invalid : Selection::Absent;
  const uint64_t size = file.fileSize64();
  if (!size || size > kMaxBytes || file.isDirectory()) { (void)file.close(); return Selection::Invalid; }
  char bytes[kMaxBytes]{};
  const bool complete = file.read(bytes, static_cast<size_t>(size)) == static_cast<int>(size);
  const bool closed = file.close();
  return complete && closed ? parse(bytes, static_cast<size_t>(size), artifact) : Selection::Invalid;
}
} // namespace RuntimeDefaultApp
