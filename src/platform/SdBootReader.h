#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Isolated, read-only boot media access. Never binds HalStorage or publishes a
// capability. A failed release retains ownership and forbids provider startup.
namespace SdBootReader {
bool mount();
bool read(const std::string& path, size_t limit, std::vector<uint8_t>& bytes);
bool release();
}
// One attempt per boot, including failures. No flash or resident-driver fallback.
bool loadPlatformSdPackages();
