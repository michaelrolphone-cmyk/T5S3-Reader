#include "UsbCdcDriverRuntime.h"
#include "DriverPackage.h"
#include "UsbCdcDriverModule.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace UsbCdcDriverRuntime {
namespace {
constexpr const char* kId = "usb-cdc-acm";
constexpr const char* kElf = "/sd/Drivers/usb-cdc-acm/driver.elf";
constexpr const char* kManifest = "/sd/Drivers/usb-cdc-acm/manifest.json";
constexpr size_t kMaxManifest = 4096;
constexpr size_t kMaxDescriptor = 4096;
UsbCdcDriverModule module;
// USB host and app tasks share the class ELF. Every entry and dlclose must
// hold this priority-inheriting mutex, which lives for the firmware lifetime.
SemaphoreHandle_t moduleMutex = nullptr;
// Neither the descriptor nor the manifest belongs on the host task stack.
uint8_t descriptorSnapshot[kMaxDescriptor];
char manifestBuffer[kMaxManifest + 1];
struct Lock {
  Lock() { xSemaphoreTake(moduleMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(moduleMutex); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

// Distinguish missing packages from damaged packages. These are internal
// diagnostics, never a reason to let an unverified ELF enter the host table.
enum class PackageCheck : uint8_t {
  Valid, StorageUnavailable, ManifestMissing, ManifestUnreadable,
  ManifestMalformed, CapabilityMismatch, PayloadInvalid,
};
const char* packageReason(PackageCheck result) {
  switch (result) {
    case PackageCheck::Valid: return "valid";
    case PackageCheck::StorageUnavailable: return "sd-unavailable";
    case PackageCheck::ManifestMissing: return "manifest-missing";
    case PackageCheck::ManifestUnreadable: return "manifest-read-failed";
    case PackageCheck::ManifestMalformed: return "manifest-json-invalid";
    case PackageCheck::CapabilityMismatch: return "manifest-capability-mismatch";
    case PackageCheck::PayloadInvalid: return "payload-verification-failed";
  }
  return "unknown";
}

PackageCheck checkPackage() {
  if (native_app_register_sd_vfs() != ESP_OK) return PackageCheck::StorageUnavailable;
  FILE* file = std::fopen(kManifest, "rb");
  if (!file) return PackageCheck::ManifestMissing;
  const size_t bytes = std::fread(manifestBuffer, 1, sizeof(manifestBuffer), file);
  const bool readOk = bytes != 0 && bytes <= kMaxManifest && !std::ferror(file);
  std::fclose(file);
  if (!readOk) return PackageCheck::ManifestUnreadable;
  const std::string manifest(manifestBuffer, bytes);
  JsonDocument doc;
  if (deserializeJson(doc, manifest)) return PackageCheck::ManifestMalformed;
  if (!doc["requires"].is<JsonArray>() || doc["requires"].size() != 1 ||
      !doc["provides"].is<JsonArray>() || doc["provides"].size() != 1)
    return PackageCheck::CapabilityMismatch;
  const char* requirement = doc["requires"][0]["capability"] | nullptr;
  const char* provision = doc["provides"][0]["capability"] | nullptr;
  if (!requirement || std::strcmp(requirement, "kernel.usb.host") != 0 ||
      doc["requires"][0]["api"].as<unsigned>() != 1 ||
      !provision || std::strcmp(provision, T5_USB_CDC_CLASS_CAPABILITY) != 0 ||
      doc["provides"][0]["api"].as<unsigned>() != T5_USB_CDC_CLASS_API_VERSION)
    return PackageCheck::CapabilityMismatch;
  DriverPackageInfo info{};
  if (!validateDriverPayload(manifest, kElf, &info) ||
      std::strcmp(info.id, kId) != 0 ||
      std::strcmp(info.capability, T5_USB_CDC_CLASS_CAPABILITY) != 0)
    return PackageCheck::PayloadInvalid;
  return PackageCheck::Valid;
}
}  // namespace

bool activate() {
  // Called on the owning USB host task before enumeration. Only a verified
  // package can enter the active driver function table.
  if (!moduleMutex) moduleMutex = xSemaphoreCreateMutex();
  if (!moduleMutex) {
    LOG_ERR("USB", "USBREF phase=driver_load result=fallback reason=mutex-allocation-failed");
    return false;
  }
  Lock lock;
  if (module.state() == UsbCdcDriverModule::State::Active) return true;
  const auto package = checkPackage();
  if (package != PackageCheck::Valid) {
    LOG_INF("USB", "USBREF phase=driver_load result=fallback reason=%s", packageReason(package));
    return false;
  }
  if (!module.load(kElf)) {
    LOG_ERR("USB", "USBREF phase=driver_load result=fallback reason=elf-load-or-abi-failed");
    return false;
  }
  LOG_INF("USB", "USBREF phase=driver_load result=active id=usb-cdc-acm api=1");
  return true;
}

bool active() {
  if (!moduleMutex) return false;
  Lock lock;
  return module.state() == UsbCdcDriverModule::State::Active;
}

bool deactivate() {
  if (!moduleMutex) return true;
  Lock lock;
  if (!module.unload()) {
    LOG_ERR("USB", "USBREF phase=driver_unload result=failed reason=module-handle-retained");
    return false;
  }
  return true;
}

bool probe(const uint8_t* configuration, size_t length, uint16_t vid, uint16_t pid,
           t5_usb_cdc_binding_v1* binding) {
  if (!moduleMutex || !configuration || !binding || length < 9 || length > kMaxDescriptor) {
    LOG_ERR("USB", "USBREF phase=driver_probe result=invalid-input vid=%04X pid=%04X bytes=%lu",
            static_cast<unsigned>(vid), static_cast<unsigned>(pid),
            static_cast<unsigned long>(length));
    return false;
  }
  bool matched = false;
  {
    Lock lock;
    std::memcpy(descriptorSnapshot, configuration, length);
    matched = module.probe(descriptorSnapshot, length, vid, pid, binding);
  }
  LOG_INF("USB", "USBREF phase=driver_probe result=%s vid=%04X pid=%04X bytes=%lu",
          matched ? "matched" : "no-match",
          static_cast<unsigned>(vid), static_cast<unsigned>(pid),
          static_cast<unsigned long>(length));
  return matched;
}

bool lineCoding(uint32_t baud, uint8_t bits, uint8_t parity, uint8_t stop,
                uint8_t payload[7]) {
  if (!moduleMutex) return false;
  Lock lock;
  return module.lineCoding(baud, bits, parity, stop, payload);
}

bool controlLines(bool dtr, bool rts, uint16_t* value) {
  if (!moduleMutex) return false;
  Lock lock;
  return module.controlLines(dtr, rts, value);
}

}  // namespace UsbCdcDriverRuntime
