#include <T5PackageApi.h>

#include "runtime/packages/PackageMvpFirmwarePolicy.h"
#include "runtime/packages/PackageDeviceCrypto.h"
#include "runtime/packages/PackageDeviceSecurityFloor.h"
#include "native/NativeAppHost.h"
#include "native/NativeSystemUiBridge.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/util/ConfirmationActivity.h"
#include "MappedInputManager.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <NativeAppLauncher.h>
#include <Logging.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
extern ActivityManager activityManager;

namespace {
using namespace RuntimePackages;
constexpr char kInboxPrefix[] = "/sd/Packages/inbox/";
constexpr char kAppPath[] = "/sd/Apps/package_manager.elf";

// The >8 KiB verification workspace and archive objects live in privileged
// firmware memory, never on a FreeRTOS task or untrusted ELF stack. Exactly
// one package-manager confirmation/installation may own these at a time.
struct ManagerMemory {
  PackageVerificationWorkspace workspace{};
  PackageArchive approved{};
  PackageArchive observed{};
};
ManagerMemory& memory() {
  static ManagerMemory instance{};
  return instance;
}
struct ResultState {
  bool available = false;
  t5_package_result_t result = T5_PACKAGE_RESULT_FAILED;
  uint64_t cookie = 0;
};
ResultState resultState{};
bool requestPending = false;

bool permittedInboxPath(const char* path) {
  if (!path || std::strncmp(path, kInboxPrefix, sizeof(kInboxPrefix) - 1) != 0)
    return false;
  const char* filename = path + sizeof(kInboxPrefix) - 1;
  if (!safePackageEntryName(filename)) return false;
  const size_t length = std::strlen(filename);
  return length > 5 && std::strcmp(filename + length - 5, ".risc") == 0;
}

bool signedPolicyAvailable() {
  const auto trust = firmwareMvpPackageTrust();
  return Storage.ready() && usableMvpPackageTrust(trust);
}

bool permittedEnrollment(const MvpFirmwareTrust& trust,
                         const PackageArchive& candidate, bool& firstInstall) {
  firstInstall = false;
  uint32_t stored = 0;
  const FloorRead floor = devicePackageSecurityFloors().read(candidate.identity.kind,
                                                             candidate.identity.id, stored);
  if (floor == FloorRead::Unavailable) return false;
  if (floor == FloorRead::Present) return stored <= candidate.securityVersion;
  if (!trust.allowFirstInstall) return false;
  SignedTransactionPaths paths{};
  if (!signedTransactionPaths(candidate.identity.kind, candidate.identity.id, paths) ||
      Storage.exists(paths.target) || Storage.exists(paths.backup)) return false;
  firstInstall = true;
  return true;
}

bool fingerprint(const PackageArchive& candidate,
                 PackageVerificationWorkspace& workspace, uint8_t out[32]) {
  if (candidate.signatureOffset < kPackageHeaderBytes + 16 ||
      candidate.signatureOffset > sizeof(workspace.signedPrefix)) return false;
  PackageMbedtlsSha256 hash;
  return hash.start() && hash.update(workspace.signedPrefix,
      static_cast<size_t>(candidate.signatureOffset)) && hash.finish(out);
}

bool inspectRequested(const char* path, MvpFirmwareTrust& trust,
                      uint8_t digest[32], bool& firstInstall,
                      std::string& title) {
  if (!signedPolicyAvailable() || !permittedInboxPath(path)) return false;
  trust = firmwareMvpPackageTrust();
  std::FILE* file = std::fopen(path, "rb");
  if (!file) return false;
  auto& m = memory();
  const bool authenticated = inspectSignedPackage(file, trust.signers,
      trust.signerCount, trust.policy, trust.resolveCapability,
      trust.resolverContext, m.workspace, m.approved) ==
          PackageInspectionResult::ContentVerifiedForInspection;
  const bool closed = std::fclose(file) == 0;
  if (!authenticated || !closed || !permittedEnrollment(trust, m.approved, firstInstall) ||
      !fingerprint(m.approved, m.workspace, digest)) {
    m.approved = {};
    return false;
  }
  title = std::string("Install signed ") + m.approved.identity.id + " " +
          m.approved.identity.version + " (INACTIVE)?";
  return true;
}

t5_package_result_t installConfirmed(const std::string& path,
    const uint8_t previouslyApproved[32], bool firstInstall,
    const MvpFirmwareTrust& trust) {
  if (!permittedInboxPath(path.c_str()) || !Storage.ready() ||
      !usableMvpPackageTrust(trust)) return T5_PACKAGE_RESULT_UNTRUSTED;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return T5_PACKAGE_RESULT_UNTRUSTED;
  auto& m = memory();
  const auto outcome = installSignedDevicePackage(file, trust.signers,
      trust.signerCount, trust.policy, trust.resolveCapability,
      trust.resolverContext, m.workspace, m.approved, m.observed,
      {}, firstInstall, false, previouslyApproved);
  const bool closed = std::fclose(file) == 0;
  if (!closed) return T5_PACKAGE_RESULT_PENDING_RECOVERY;
  switch (outcome.result) {
    case SignedInstallResult::Installed:
      return T5_PACKAGE_RESULT_INSTALLED_INACTIVE;
    case SignedInstallResult::IntakeRejected:
      return T5_PACKAGE_RESULT_UNTRUSTED;
    case SignedInstallResult::StaleOrForeignStage:
      return T5_PACKAGE_RESULT_STALE_STAGE;
    case SignedInstallResult::PublicationPendingRecovery:
    case SignedInstallResult::InstalledIntakeCleanupPending:
      return T5_PACKAGE_RESULT_PENDING_RECOVERY;
    default:
      return T5_PACKAGE_RESULT_FAILED;
  }
}

class NativeSignedInstallActivity final : public Activity {
 public:
  NativeSignedInstallActivity(GfxRenderer& screen, MappedInputManager& input,
      std::string resume, std::string path, std::string title,
      uint64_t cookie, const uint8_t digest[32],
      bool firstInstall, MvpFirmwareTrust trust)
      : Activity("NativeSignedInstall", screen, input), resume_(std::move(resume)),
        path_(std::move(path)), title_(std::move(title)), cookie_(cookie),
        firstInstall_(firstInstall), trust_(trust) {
    std::memcpy(digest_, digest, sizeof(digest_));
  }

  void onEnter() override {
    Activity::onEnter();
    if (started_) return;
    started_ = true;
    startActivityForResult(std::make_unique<ConfirmationActivity>(renderer,
        mappedInput, title_, "Firmware will verify and store the package; it will NOT be activated."),
        [this](const ActivityResult& reply) {
          confirmed_ = !reply.isCancelled;
          consentReturned_ = true;
        });
  }
  void loop() override {
    if (resumeReturned_) { finish(); return; }
    if (!consentReturned_) return;
    consentReturned_ = false;
    resultState.result = confirmed_ ?
        installConfirmed(path_, digest_, firstInstall_, trust_) :
        T5_PACKAGE_RESULT_CANCELLED;
    resultState.cookie = cookie_;
    resultState.available = true;
    requestPending = false;
    if (resume_.empty() || runNativeApp(resume_.c_str(), renderer,
                                         mappedInput) != ESP_OK) {
      resultState = {};
      finish();
      return;
    }
    resumeReturned_ = true;
  }
  void render(RenderLock&&) override {}

 private:
  std::string resume_;
  std::string path_;
  std::string title_;
  uint64_t cookie_;
  bool firstInstall_ = false;
  MvpFirmwareTrust trust_{};
  uint8_t digest_[32]{};
  bool started_ = false;
  bool consentReturned_ = false;
  bool confirmed_ = false;
  bool resumeReturned_ = false;
};

bool requestInstall(const char* path, uint64_t cookie) {
  const char* current = native_app_current_path();
  if (!current || std::strcmp(current, kAppPath) != 0 ||
      requestPending || resultState.available || !permittedInboxPath(path))
    return false;
  MvpFirmwareTrust trust{};
  uint8_t digest[32]{};
  bool firstInstall = false;
  std::string heading;
  if (!inspectRequested(path, trust, digest, firstInstall, heading)) return false;
  requestPending = true;
  activityManager.pushActivity(std::make_unique<NativeSignedInstallActivity>(
      renderer, mappedInputManager, current, path, heading, cookie,
      digest, firstInstall, trust));
  nativeSystemUiMarkActivityPending();
  return true;
}

bool takeResult(t5_package_result_t* result, uint64_t* cookie) {
  if (!resultState.available || !result || !cookie) return false;
  *result = resultState.result;
  *cookie = resultState.cookie;
  resultState = {};
  return true;
}

const t5_package_api_v1 api = {T5_PACKAGE_API_VERSION,
    sizeof(t5_package_api_v1), signedPolicyAvailable, requestInstall, takeResult};
} // namespace

extern "C" const t5_package_api_v1* t5_package_get_api(uint32_t version) {
  return version == T5_PACKAGE_API_VERSION ? &api : nullptr;
}
