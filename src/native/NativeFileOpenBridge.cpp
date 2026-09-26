#include <T5AppApi.h>
#include <T5FileOpenApi.h>

#include "FileAssociationRegistry.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <MappedInputManager.h>
#include <NativeAppLauncher.h>
#include <esp_err.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "NativeAppHost.h"
#include "NativeSystemUiBridge.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {

struct OpenResult {
  bool available = false;
  int32_t error = 0;
  uint64_t cookie = 0;
};

OpenResult openResult;
std::string activeSourcePath;

bool active() {
  return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr;
}

bool validSdPath(const char* path) {
  if (!path || std::strncmp(path, "/sd/", 4) != 0 || !path[4]) return false;
  const size_t length = std::strlen(path);
  if (length >= T5_FILE_OPEN_PATH_MAX) return false;
  for (size_t i = 0; i < length; ++i)
    if (static_cast<unsigned char>(path[i]) < 32) return false;
  return true;
}

bool sourceExists(const char* path) {
  if (!validSdPath(path)) return false;
  FILE* file = std::fopen(path, "rb");
  if (!file) return false;
  std::fclose(file);
  return true;
}

class NativeFileOpenActivity final : public Activity {
  std::string resumePath;
  std::string targetPath;
  std::string sourcePath;
  uint64_t cookie;
  bool ranTarget = false;
  bool resumeReturned = false;

 public:
  NativeFileOpenActivity(GfxRenderer& gfxRenderer, MappedInputManager& input,
                         std::string resume, std::string target,
                         std::string source, uint64_t requestCookie)
      : Activity("NativeFileOpen", gfxRenderer, input),
        resumePath(std::move(resume)),
        targetPath(std::move(target)),
        sourcePath(std::move(source)),
        cookie(requestCookie) {}

  void loop() override {
    if (resumeReturned) {
      finish();
      return;
    }

    if (!ranTarget) {
      ranTarget = true;
      activeSourcePath = sourcePath;
      openResult.available = true;
      openResult.error = runNativeApp(targetPath.c_str(), renderer, mappedInput);
      openResult.cookie = cookie;
      activeSourcePath.clear();
    }

    if (resumePath.empty() ||
        runNativeApp(resumePath.c_str(), renderer, mappedInput) != ESP_OK) {
      openResult = {};
      finish();
      return;
    }
    resumeReturned = true;
  }

  void render(RenderLock&&) override {}
};

bool refreshAssociations() {
  return active() && NativeFileAssociations::rebuild();
}

uint32_t handlerCount(const char* sourcePath) {
  if (!active() || !validSdPath(sourcePath)) return 0;
  return NativeFileAssociations::countForPath(sourcePath);
}

bool handlerGet(const char* sourcePath, uint32_t index, t5_file_handler_t* out) {
  if (out) *out = {};
  if (!active() || !out || !validSdPath(sourcePath)) return false;
  NativeFileAssociations::Handler handler{};
  if (!NativeFileAssociations::getForPath(sourcePath, index, handler)) return false;
  out->kind = handler.kind == NativeFileAssociations::HandlerKind::App
                  ? T5_FILE_HANDLER_APP
                  : T5_FILE_HANDLER_SYSTEM_READER;
  std::snprintf(out->app_id, sizeof(out->app_id), "%s", handler.appId);
  std::snprintf(out->display_name, sizeof(out->display_name), "%s", handler.displayName);
  std::snprintf(out->icon, sizeof(out->icon), "%s", handler.icon);
  return true;
}

bool openRequest(const char* sourcePath, const char* appId, uint64_t cookie) {
  const char* currentPath = native_app_current_path();
  if (!active() || !validSdPath(currentPath) || !validSdPath(sourcePath) ||
      !appId || !appId[0] || openResult.available || !sourceExists(sourcePath))
    return false;

  NativeFileAssociations::Handler handler{};
  if (!NativeFileAssociations::hasAppForPath(sourcePath, appId, &handler) ||
      handler.kind != NativeFileAssociations::HandlerKind::App ||
      !validSdPath(handler.launchPath))
    return false;

  activityManager.pushActivity(std::make_unique<NativeFileOpenActivity>(
      renderer, mappedInputManager, std::string(currentPath),
      std::string(handler.launchPath), std::string(sourcePath), cookie));
  nativeSystemUiMarkActivityPending();
  return true;
}

bool openTakeResult(int32_t* espError, uint64_t* cookie) {
  if (!openResult.available) return false;
  if (espError) *espError = openResult.error;
  if (cookie) *cookie = openResult.cookie;
  openResult = {};
  return true;
}

bool sourcePathGet(char* out, size_t capacity) {
  if (!active() || !out || !capacity || activeSourcePath.empty() ||
      activeSourcePath.size() >= capacity)
    return false;
  std::memcpy(out, activeSourcePath.c_str(), activeSourcePath.size() + 1);
  return true;
}

const t5_file_open_api_v1 api = {
    T5_FILE_OPEN_API_VERSION,
    sizeof(t5_file_open_api_v1),
    refreshAssociations,
    handlerCount,
    handlerGet,
    openRequest,
    openTakeResult,
    sourcePathGet,
};

}  // namespace

extern "C" const t5_file_open_api_v1* t5_file_open_get_api(uint32_t version) {
  return version == T5_FILE_OPEN_API_VERSION && active() ? &api : nullptr;
}
