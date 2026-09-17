#pragma once

#include <AppManifestRules.h>
#include <cstring>
#include <string>

namespace RuntimePackages {

// Translate only files belonging to the legacy managed /Apps pair transaction
// into a validated ELF basename. An orphan .json alone is intentionally not
// sufficient: other applications may keep unrelated JSON data in /Apps.
// This parser does not touch the filesystem, authenticate content or recover it.
inline bool appRecoveryCandidate(const char* storedName, std::string& elfName) {
  elfName.clear();
  if (!storedName) return false;
  const size_t length = std::strlen(storedName);
  constexpr const char* suffixes[] = {
      ".elf.bak", ".json.bak", ".elf.part", ".json.part", ".elf"};
  for (const char* suffix : suffixes) {
    const size_t suffixLength = std::strlen(suffix);
    if (length <= suffixLength || std::strcmp(storedName + length - suffixLength, suffix)) continue;
    const size_t stemLength = length - suffixLength;
    if (stemLength + 4 >= 128) return false;
    std::string candidate(storedName, stemLength);
    candidate += ".elf";
    if (!t5_safe_elf_name(candidate.c_str())) return false;
    elfName.swap(candidate);
    return true;
  }
  return false;
}

} // namespace RuntimePackages
