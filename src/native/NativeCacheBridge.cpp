#include <T5AppApi.h>
#include <T5CacheApi.h>

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

namespace {

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

bool clearReadingCache(t5_cache_clear_result_t* out) {
  if (!active() || !out) return false;
  std::memset(out, 0, sizeof(*out));

  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    out->directory_available = 0;
    return true;
  }

  out->directory_available = 1;
  char name[128];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);
    if (file.isDirectory() && (itemName.startsWith("epub_") || itemName.startsWith("xtc_"))) {
      String fullPath = "/.crosspoint/" + itemName;
      file.close();
      if (Storage.removeDir(fullPath.c_str())) {
        ++out->removed_count;
      } else {
        LOG_ERR("CLEAR_CACHE", "Failed to remove: %s", fullPath.c_str());
        ++out->failed_count;
      }
    } else {
      file.close();
    }
  }
  root.close();
  return true;
}

const t5_cache_api_v1 api = {
    T5_CACHE_API_VERSION,
    sizeof(t5_cache_api_v1),
    clearReadingCache,
};

}  // namespace

extern "C" const t5_cache_api_v1* t5_cache_get_api(uint32_t version) {
  return version == T5_CACHE_API_VERSION && active() ? &api : nullptr;
}
