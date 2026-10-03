#include "PlatformStorage.h"
#include "FlashModuleStore.h"
#include "runtime/drivers/BootstrapModuleStore.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include <Board.h>
#include <HalStorage.h>
#include <Logging.h>

namespace {
  RuntimeInstalledProviders::Lease platformLeases[3];
  auto& expanderLease = platformLeases[0];
  auto& frontlightLease = platformLeases[1];
  auto& storageLease = platformLeases[2];
  bool boardAttempted = false, boardComposed = false;
  bool storageAttempted = false, storageComposed = false;
}
bool beginPlatformBoardProviders() {
#if defined(BOARD_T5S3_PRO)
    if (boardAttempted) return boardComposed;
    boardAttempted = true;
    if (!mountFlashModuleStore() ||
        !RuntimeInstalledProviders::loadBootstrapPackages("/bootfs", Board::id()) ||
        !RuntimeInstalledProviders::acquireCapability("gpio.expander", 1, &expanderLease) ||
        !Board::attachExpander(static_cast<const risc_gpio_expander_api_v1*>(expanderLease.interface)) ||
        !RuntimeInstalledProviders::acquireCapability("display.frontlight", 1, &frontlightLease) ||
        !Board::attachFrontlight(static_cast<const risc_frontlight_api_v1*>(frontlightLease.interface))) {
        LOG_ERR("BOARD", "External board provider unavailable; no peripheral fallback");
        return false;
    }
    boardComposed = true;
#endif
    return true;
}
bool beginPlatformStorage() {
#if defined(BOARD_T5S3_PRO)
    if (storageAttempted) return storageComposed && Storage.ready();
    storageAttempted = true;
    if (!beginPlatformBoardProviders() ||
        !RuntimeInstalledProviders::acquireCapability("storage.volume", 1, &storageLease)) {
        LOG_ERR("STO", "External storage provider unavailable; no firmware filesystem fallback");
        return false;
    }
    storageComposed = true;
    return Storage.bindVolume(static_cast<const risc_storage_volume_api_v1*>(storageLease.interface));
#else
    return Storage.begin();
#endif
}
bool drainPlatformProvidersForSleep() {
#if defined(BOARD_T5S3_PRO)
    // Includes partial startup grants, never drops an uncertain chip owner.
    size_t count = 0;
    while (count < 3 && platformLeases[count].grant.slot) ++count;
    if (count) return RuntimeInstalledProviders::drainExcept(platformLeases, count);
#endif
    return RuntimeInstalledProviders::shutdown();
}
