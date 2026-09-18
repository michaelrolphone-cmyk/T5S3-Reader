#pragma once

#include "NativeOnlinePackageRecovery.h"
#include <HalStorage.h>
#include <cstdint>
#include <cstring>
#include <string>

namespace RuntimeOnlinePackages {
namespace Recovery {

// A physical provider owns a deterministic four-file inbox. Recover only
// the matching release descriptor and fixed expected filenames; an unknown
// entry, altered descriptor, oversized download, or foreign subdirectory
// blocks deletion. Incomplete .part files may be removed only in that known
// source directory, never through paths supplied by release metadata.
inline bool discardMatchingDriverInbox(const std::string& root,
                                       const std::string& descriptor,
                                       const char* const (&names)[4],
                                       const uint64_t (&sizes)[4]) {
  if (descriptor.empty() || descriptor.size() > 4096 ||
      !equalFile(root + "/.package.json", descriptor.data(), descriptor.size())) return false;
  HalFile directory = Storage.open(root.c_str(), O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) {
    if (directory.isOpen()) (void)directory.close();
    return false;
  }
  bool seen[4]{}, partial[4]{}, valid = true;
  for (;;) {
    HalFile file = directory.openNextFile();
    if (!file.isOpen()) break;
    char name[128]{};
    const size_t length = file.getName(name, sizeof(name));
    const uint64_t bytes = file.fileSize64();
    const bool regular = length && length < sizeof(name) && !file.isDirectory();
    (void)file.close();
    if (!regular) { valid = false; break; }
    bool found = false;
    for (size_t i = 0; i < 4; ++i) {
      if (std::strcmp(name, names[i]) == 0 && !seen[i] &&
          bytes <= sizes[i]) { seen[i] = found = true; break; }
      if (i && std::string(name) == std::string(names[i]) + ".part" &&
          !partial[i] && bytes <= sizes[i]) { partial[i] = found = true; break; }
    }
    if (!found) { valid = false; break; }
  }
  const bool closed = directory.close();
  if (!valid || !closed || !seen[0]) return false;
  // Remove the descriptor LAST, preserving a recognizable failed intake if
  // power fails during the cleanup itself.
  for (size_t i = 1; i < 4; ++i) {
    if (partial[i] && !Storage.remove((root + "/" + names[i] + ".part").c_str()))
      return false;
    if (seen[i] && !Storage.remove((root + "/" + names[i]).c_str()))
      return false;
  }
  return Storage.remove((root + "/.package.json").c_str()) && Storage.rmdir(root.c_str());
}

} // namespace Recovery
} // namespace RuntimeOnlinePackages
