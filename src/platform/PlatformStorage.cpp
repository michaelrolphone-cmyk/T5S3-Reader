#include "PlatformStorage.h"
#include "FlashModuleStore.h"
#include "runtime/drivers/BootstrapModuleStore.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include <Board.h>
#include <HalStorage.h>
#include <Logging.h>

namespace { RuntimeInstalledProviders::Lease platformLeases[2];
  auto& storageLease = platformLeases[0];
  auto& frontlightLease = platformLeases[1];
  bool attempted = false, composed = false;
}
bool beginPlatformStorage() {
#if defined(BOARD_T5S3_PRO)
    if (attempted) return composed && Storage.ready();
    attempted = true;
    if (!mountFlashModuleStore() ||
        !RuntimeInstalledProviders::loadBootstrapPackages("/bootfs", Board::id()) ||
        !RuntimeInstalledProviders::acquireCapability("storage.volume", 1, &storageLease)) {
        LOG_ERR("STO", "External storage provider unavailable; no firmware filesystem fallback");
        return false;
    }
    const bool mounted = Storage.bindVolume(static_cast<const risc_storage_volume_api_v1*>(storageLease.interface));
    if (!RuntimeInstalledProviders::acquireCapability("display.frontlight", 1, &frontlightLease) ||
        !Board::attachFrontlight(static_cast<const risc_frontlight_api_v1*>(frontlightLease.interface))) {
        LOG_ERR("STO", "External frontlight provider unavailable; no firmware PWM fallback");
        return false;
    }
    composed = true;
    return mounted;
#else
    return Storage.begin();
#endif
}
bool drainPlatformProvidersForSleep() {
#if defined(BOARD_T5S3_PRO)
    if (storageLease.grant.slot)
        return RuntimeInstalledProviders::drainExcept(platformLeases, frontlightLease.grant.slot ? 2 : 1);
#endif
    return RuntimeInstalledProviders::shutdown();
}
