#include "AppPackageInstaller.h"
#include "runtime/packages/PackageAppRecoveryIndex.h"

#include <HalStorage.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <string>
#include <vector>

namespace RuntimePackages {

bool recoverAppInventory() {
  if (!Storage.ready()) return false;
  HalFile directory = Storage.open("/Apps", O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) {
    if (directory.isOpen()) directory.close();
    return false;
  }

  // Enumerate first and close the directory before renaming anything. A
  // directory iterator cannot safely survive pair-transaction mutations.
  std::vector<std::string> candidates;
  candidates.reserve(128);
  bool complete = true;
  size_t entries = 0;
  for (;;) {
    HalFile file = directory.openNextFile();
    if (!file.isOpen()) break;
    if (++entries > 1024) {
      file.close();
      complete = false;
      break;
    }
    char name[160]{};
    const size_t length = file.getName(name, sizeof(name));
    const bool isDirectory = file.isDirectory();
    file.close();
    if (isDirectory) continue;
    if (length == 0 || length >= sizeof(name)) {
      complete = false;
      break;
    }
    std::string elf;
    if (!appRecoveryCandidate(name, elf)) continue;
    if (std::find(candidates.begin(), candidates.end(), elf) != candidates.end()) continue;
    if (candidates.size() >= 256) {
      complete = false;
      break;
    }
    candidates.push_back(std::move(elf));
    if ((entries & 31u) == 0) esp_task_wdt_reset();
  }
  directory.close();
  if (!complete) {
    LOG_ERR("APPSTORE", "App inventory too large or unreadable; refusing partial recovery");
    return false;
  }

  bool allRecovered = true;
  for (const auto& elf : candidates) {
    const std::string target = std::string("/Apps/") + elf;
    const std::string manifest = target.substr(0, target.size() - 4) + ".json";
    const bool backedUp = Storage.exists((target + ".bak").c_str()) ||
                          Storage.exists((manifest + ".bak").c_str());
    const bool staged = Storage.exists((target + ".part").c_str()) ||
                        Storage.exists((manifest + ".part").c_str());
    // A deliberately loose ELF in /Apps without a sidecar or transaction
    // artifacts is not a managed pair. Preserve the file-browser contract.
    if (!Storage.exists(manifest.c_str()) && !backedUp && !staged) continue;
    const char* active = native_app_current_path();
    const std::string mapped = std::string("/sd") + target;
    // Recovery can rename an executable during rollback. Never rename the
    // mapped application out from under the running owner context.
    if (active && mapped == active && backedUp) {
      LOG_ERR("APPSTORE", "Recovery deferred for mapped package %s", elf.c_str());
      allRecovered = false;
      continue;
    }
    if (!recoverAppPair(elf.c_str())) allRecovered = false;
    esp_task_wdt_reset();
  }
  return allRecovered;
}

} // namespace RuntimePackages
