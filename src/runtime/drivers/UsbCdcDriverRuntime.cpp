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
UsbCdcDriverModule module;
// The USB host task handles attach and shutdown while the native app task can
// reconfigure an active serial port. Every ELF call, including dlclose, must
// hold the same priority-inheriting mutex. The lock lasts for the firmware
// lifetime; it is not destroyed while either task may still be running.
SemaphoreHandle_t moduleMutex = nullptr;
struct Lock {
  Lock() { xSemaphoreTake(moduleMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(moduleMutex); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

bool installedAndVerified() {
  if (native_app_register_sd_vfs() != ESP_OK) return false;
  FILE* file = std::fopen(kManifest, "rb");
  if (!file) return false;
  char buffer[kMaxManifest + 1] = {};
  const size_t bytes = std::fread(buffer, 1, sizeof(buffer), file);
  const bool readOk = bytes != 0 && bytes <= kMaxManifest && !std::ferror(file);
  std::fclose(file);
  if (!readOk) return false;
  const std::string manifest(buffer, bytes);
  JsonDocument doc;
  if (deserializeJson(doc, manifest)) return false;
  if (!doc["requires"].is<JsonArray>() || doc["requires"].size() != 1 ||
      !doc["provides"].is<JsonArray>() || doc["provides"].size() != 1) return false;
  const char* requirement = doc["requires"][0]["capability"] | nullptr;
  const char* provision = doc["provides"][0]["capability"] | nullptr;
  if (!requirement || std::strcmp(requirement, "kernel.usb.host") != 0 ||
      doc["requires"][0]["api"].as<unsigned>() != 1 ||
      !provision || std::strcmp(provision, T5_USB_CDC_CLASS_CAPABILITY) != 0 ||
      doc["provides"][0]["api"].as<unsigned>() != T5_USB_CDC_CLASS_API_VERSION) return false;
  DriverPackageInfo info{};
  return validateDriverPayload(manifest, kElf, &info) &&
         std::strcmp(info.id, kId) == 0 &&
         std::strcmp(info.capability, T5_USB_CDC_CLASS_CAPABILITY) == 0;
}
}  // namespace

bool activate() {
  // activate() is the only initializer; it executes on the USB host task
  // before enumeration, so no other task can reach an ELF entry point yet.
  if (!moduleMutex) moduleMutex = xSemaphoreCreateMutex();
  if (!moduleMutex) return false;
  Lock lock;
  if (module.state() == UsbCdcDriverModule::State::Active) return true;
  if (!installedAndVerified()) {
    LOG_INF("USB", "Installable CDC package missing/invalid; using resident USB drivers");
    return false;
  }
  if (!module.load(kElf)) {
    LOG_ERR("USB", "Verified CDC ELF failed ABI validation/start; using resident drivers");
    return false;
  }
  LOG_INF("USB", "usb-cdc-acm ELF ACTIVE: usb.class.cdc_acm API 1");
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
    LOG_ERR("USB", "CDC ELF unload failed; module handle retained");
    return false;
  }
  return true;
}

bool probe(const uint8_t* configuration, size_t length, uint16_t vid, uint16_t pid,
           t5_usb_cdc_binding_v1* binding) {
  if (!moduleMutex) return false;
  Lock lock;
  return module.probe(configuration, length, vid, pid, binding);
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
