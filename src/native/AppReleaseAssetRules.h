#pragma once
#include <cctype>
#include <string>

namespace NativeAppReleaseRules {

inline bool endsWithInsensitive(const std::string& value, const char* suffix) {
  if (!suffix) return false;
  size_t length = 0;
  while (suffix[length]) ++length;
  if (value.size() < length) return false;
  const size_t start = value.size() - length;
  for (size_t i = 0; i < length; ++i) {
    if (std::tolower(static_cast<unsigned char>(value[start + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) return false;
  }
  return true;
}

inline bool appElfAssetName(const std::string& name) {
  if (name.empty() || name.find("..") != std::string::npos ||
      name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
      !endsWithInsensitive(name, ".elf")) return false;

  // Legacy driver bundles and canonical provider assets are release ELFs too,
  // but they are not launchable applications and must never enter App Store
  // manifest fallback.
  if (endsWithInsensitive(name, ".t5driver.elf") ||
      endsWithInsensitive(name, "--driver.elf")) return false;
  return true;
}

}  // namespace NativeAppReleaseRules
