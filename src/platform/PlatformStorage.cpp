#include <T5VideoApi.h>
#include "PlatformStorage.h"
#include "FlashModuleStore.h"
#include "runtime/drivers/BootstrapModuleStore.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include <Board.h>
#include <PlatformDisplayPower.h>
#include <PlatformDisplayProvider.h>
#include <HalStorage.h>
#include <Logging.h>

namespace {
  RuntimeInstalledProviders::Lease platformLeases[5];
  auto& expanderLease = platformLeases[0];
  auto& frontlightLease = platformLeases[1];
  auto& powerLease = platformLeases[2];
  auto& storageLease = platformLeases[3];
  auto& displayLease = platformLeases[4];
  bool boardAttempted = false, boardComposed = false;
  bool storageAttempted = false, storageComposed = false;
}
const risc_display_power_api_v1* platformDisplayPower() {
    return static_cast<const risc_display_power_api_v1*>(powerLease.interface);
}
const t5_display_provider_api_v1* platformDisplayProvider() {
#if defined(BOARD_T5S3_PRO)
    if (!beginPlatformBoardProviders()) return nullptr;
    if (!displayLease.grant.slot &&
        !RuntimeInstalledProviders::acquireCapability("display.output", 1, &displayLease)) return nullptr;
    const auto* api = static_cast<const t5_display_provider_api_v1*>(displayLease.interface);
    if (!api || api->base.struct_size < sizeof(*api) || api->base.api_version != 1 ||
        api->extension_tag != T5_DISPLAY_EXTENSION_TAG || api->extension_version != 1 ||
        !api->quality || api->quality->api_version != 1 || api->quality->struct_size < sizeof(*api->quality) ||
        !api->quality->start || !api->quality->write_gray || !api->quality->wait ||
        !api->quality->set_power || !api->quality->suppress_output || !api->quality->try_stop ||
        !api->fast || api->fast->api_version != 1 || api->fast->struct_size < sizeof(*api->fast)) return nullptr;
    return api;
#else
    return nullptr;
#endif
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
        !Board::attachFrontlight(static_cast<const risc_frontlight_api_v1*>(frontlightLease.interface)) ||
        !RuntimeInstalledProviders::acquireCapability("display.power", 1, &powerLease)) {
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
    RuntimeInstalledProviders::Lease retained[5];
    size_t count = 0;
    for (const auto& lease : platformLeases) if (lease.grant.slot) retained[count++] = lease;
    if (count) return RuntimeInstalledProviders::drainExcept(retained, count);
#endif
    return RuntimeInstalledProviders::shutdown();
}
