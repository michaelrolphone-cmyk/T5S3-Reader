#if defined(RISCRTE_PROFILE_HEADLESS)
#include "HeadlessRuntime.h"
#include "HeadlessLifecycle.h"
#include "HeadlessAppHost.h"
#include "native/NativeStreamBridge.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageMutationGate.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageCdcSdMigration.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <NativeAppLauncher.h>
#include <RiscPlatformClockV1.h>
#include <RiscArchiveZipV1.h>
#include "HalStorageSdmmcControl.h"

#ifdef RISCRTE_CAM_QUALIFICATION
#include "../../../test/hardware/cam/Qualification.h"
#endif

#ifdef RISCRTE_CAM_APP_EXPERIMENT
#include "../../../test/hardware/cam/CameraAppInstall.h"
#endif

#ifdef RISCRTE_CAM_CAMERA_EXPERIMENT
#include "../../../test/hardware/cam/CameraProof.h"
#include "../../../test/hardware/cam/CameraInstall.h"
#endif
namespace RuntimeBoot {
namespace {
namespace Packages = RuntimePackages;
namespace Providers = RuntimeInstalledProviders;
// Trusted deployment profile, independent of installation. This first offline
// CAM profile grants only these two non-peripheral services. No directory scan
// can expand the activation list. Provisioning remains absent; a verified
// configured application may request its own declared capabilities.
struct Request { Packages::Kind kind; const char* id; const char* capability; uint32_t version; };
constexpr Request requests[] = {
  {Packages::Kind::Driver, "platform-clock-v1", "platform.clock", 1},
  {Packages::Kind::Service, "archive-zip", "archive.zip", 1},
#ifdef RISCRTE_CAM_CAMERA_EXPERIMENT
  {Packages::Kind::Driver, "cam-ov3660-profile", "board.camera.esp32s3.profile", 1},
  {Packages::Kind::Driver, "camera-esp32s3-ov3660", "camera.capture", 1},
#endif
};
constexpr Packages::PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 8u*1024u*1024u, 16u*1024u*1024u};
class Adapter {
 public:
  size_t count() const { return sizeof(requests)/sizeof(requests[0]); }
  Result mount() {
    if (!Storage.begin()) return Result::Absent;
    return native_app_register_sd_vfs() == ESP_OK ? Result::Ready : Result::Fault;
  }
  Result recover(size_t index) {
    const auto& request = requests[index];
    Packages::ScopedPackageMutation mutation;
    if (!mutation) return Result::Fault;
    struct Ops {
      bool exists(const char* p) { return Storage.exists(p); }
      bool rename(const char* a, const char* b) { return Storage.rename(a,b); }
    } ops;
    const auto verify = [&request](const char* path, Packages::Identity& observed) {
      return Packages::verifyManagedOrdinarySdDirectory(path, request.kind, request.id,
        policy, Packages::installedCapabilityVersion, observed);
    };
    const auto purge = [&request](const char* path) {
      return Packages::purgeManagedOrdinarySdDirectory(path, request.kind, request.id);
    };
    Packages::Identity observed{};
    Packages::OrdinarySdLineageTransaction transaction{policy, Packages::installedCapabilityVersion};
    const auto result = transaction.recover(ops, request.kind, request.id, verify, purge, observed);
    LOG_INF("BOOT", "recover id=%s result=%u", request.id, unsigned(result));
    if (!Storage.ready()) return Result::Fault;
    using R = Packages::OrdinaryTransactionResult;
    if (result == R::InstalledVerified || result == R::PreviousRestored) return Result::Ready;
    if (result == R::NoInstalledPackage || result == R::Removed) return Result::Absent;
    return Result::Fault;
  }
  Result inventory() {
    auto* snapshot = Packages::captureInstalledCapabilities();
    if (!snapshot) return Result::Fault;
    bool present = true;
    for (const auto& request : requests) {
      const auto version = Packages::versionInInstalledSnapshot(snapshot, request.capability);
      LOG_INF("BOOT", "inventory capability=%s version=%u", request.capability, unsigned(version));
      present = present && version >= request.version;
    }
    Packages::releaseInstalledCapabilities(snapshot);
    return present ? Result::Ready : Result::Absent;
  }
  Result prepare() { return Providers::prepare() ? Result::Ready : Result::Fault; }
  Result bind(size_t index) {
    const auto& request = requests[index];
    const bool acquired = Providers::acquire(request.id, request.capability, request.version, &leases[index]);
    LOG_INF("BOOT", "bind id=%s granted=%u error=%.120s", request.id, acquired, acquired ? "none" : Providers::lastError());
    return acquired ? Result::Ready : Result::Fault;
  }
  void poll() { nativeProviderOwnerTick(); }
  bool shutdown() {
    // Reverse dependency order. Attempt all releases, but never destroy the
    // graph or mount while even one grant/quiescence remains uncertain.
    bool closed = true;
    for (size_t n=count(); n; --n)
      if (leases[n-1].grant.slot && !Providers::release(&leases[n-1])) closed = false;
    return closed && Providers::shutdown();
  }
  bool unmount() { return BootstrapHalStorage::releaseForHandoff(); }
  Providers::Lease leases[sizeof(requests)/sizeof(requests[0])]{};
};
Adapter adapter;
HeadlessLifecycle<Adapter> lifecycle(adapter);
uint32_t lastLog = 0;
State previous = State::Cold;
const char* stateName(State s) {
  switch(s) {
#define STATE(x) case State::x: return #x
    STATE(Cold); STATE(Mount); STATE(Recover); STATE(Inventory); STATE(Prepare);
    STATE(Bind); STATE(Running); STATE(Idle); STATE(Stopping); STATE(Retained);
#undef STATE
  }
  return "invalid";
}
}
void setup() {
  Serial.begin(115200);
  Serial.println(); // Separate any partial SDK diagnostic from structured boot logs.
  LOG_INF("BOOT", "profile=cam-offline u1=480bf345 gui=disabled nvs=disabled provisioning=absent");
  uint8_t mac[6]{}; esp_efuse_mac_get_default(mac);
  LOG_INF("BOOT", "mac=%02x:%02x:%02x:%02x:%02x:%02x app=0x%x flash=%u psram=%u",
    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], unsigned(esp_ota_get_running_partition()->address),
    ESP.getFlashChipSize(), ESP.getPsramSize());
#ifdef RISCRTE_CAM_APP_EXPERIMENT
  if(!installCameraAppExperiment()){LOG_ERR("APP","result=failed stage=install");return;}
#endif
#ifdef RISCRTE_CAM_CAMERA_EXPERIMENT
  // Storage port verifies the dedicated CAM MAC before any filesystem I/O.
  // This selected profile reserves camera pins/LCD_CAM/GDMA RX4 exclusively;
  // no other physical provider may activate in this closed lab deployment.
  if(!installCameraExperiment()){LOG_ERR("CAMERA","result=failed stage=install");return;}
#endif
  lifecycle.start();
}
void loop() {
  lifecycle.tick();
  const auto state = lifecycle.state();
  if (state != previous) {
    LOG_INF("BOOT", "state=%s", stateName(state));
    previous = state;
  }
  static bool defaultAppChecked = false;
  if (state == State::Running && !defaultAppChecked) {
    defaultAppChecked = true;
    runConfiguredDefaultApp();
  }
  if (millis()-lastLog >= 5000) {
    lastLog = millis();
    LOG_INF("BOOT", "heartbeat state=%s heap=%u psram=%u handles=%u has_grants=%u",
      stateName(state), ESP.getFreeHeap(), ESP.getFreePsram(),
      BootstrapHalStorage::openHandleCount(), Providers::hasLiveGrants());
  }
#ifdef RISCRTE_CAM_QUALIFICATION
  qualificationTick(lifecycle, adapter);
#endif
#ifdef RISCRTE_CAM_CAMERA_EXPERIMENT
  cameraLifecycleProofTick(lifecycle,adapter);
#endif
  // Same owner task as setup, graph and bootstrap storage; independent of UI.
  vTaskDelay(pdMS_TO_TICKS(20)+1);
}
} // namespace RuntimeBoot
#endif
