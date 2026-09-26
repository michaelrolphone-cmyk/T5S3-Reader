#pragma once

#include <cstddef>
#include <cstdint>

namespace NativeFileAssociations {

constexpr size_t kExtensionBytes = 16;
constexpr size_t kAppIdBytes = 64;
constexpr size_t kDisplayNameBytes = 96;
constexpr size_t kIconBytes = 24;
constexpr size_t kLaunchPathBytes = 192;
constexpr size_t kMaxHandlers = 128;

enum class HandlerKind : uint8_t {
  App = 0,
  SystemReader = 1,
};

struct Handler {
  HandlerKind kind = HandlerKind::App;
  char extension[kExtensionBytes]{};
  char appId[kAppIdBytes]{};
  char displayName[kDisplayNameBytes]{};
  char icon[kIconBytes]{};
  char launchPath[kLaunchPathBytes]{};
};

// Re-scan verified installed app manifests and atomically refresh the persisted
// association manifest at /.crosspoint/file-associations.json.
bool rebuild();
void invalidate();

// Lazy repair path for boot/power-loss cases where a package mutation happened
// before the association manifest could be rewritten.
bool ensure();

uint32_t countForPath(const char* sdVfsPath);
bool getForPath(const char* sdVfsPath, uint32_t index, Handler& out);
bool hasAppForPath(const char* sdVfsPath, const char* appId, Handler* out = nullptr);

}  // namespace NativeFileAssociations
