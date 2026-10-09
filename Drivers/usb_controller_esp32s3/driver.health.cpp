/* Explicit native-PHY/health profile. The default translation units and their
 * cleanup behavior remain unchanged. Only this selected unit observes failed
 * cleanup at the IDF boundary; no health check calls hardware or a dependency. */
#if !RISC_USB_CONTROLLER_NATIVE_PHY_LEASE
#error "Provider health requires the explicit native-PHY profile"
#endif
#include <usb/usb_host.h>
#include <esp_private/usb_phy.h>
#include "RiscProviderHealthV1.h"
#include "RiscUsbControllerV1.h"
namespace {
static bool health_cleanup_failed;
static usb_device_handle_t health_unassigned_device;
static bool health_bulk_draining;
static bool health_interrupt_draining[RISC_USB_HOST_MAX_CLAIMS];
static esp_err_t health_endpoint_halt(usb_device_handle_t, uint8_t);
static esp_err_t health_endpoint_flush(usb_device_handle_t, uint8_t);
static esp_err_t health_endpoint_clear(usb_device_handle_t, uint8_t);
static esp_err_t health_transfer_free(usb_transfer_t *);
static esp_err_t health_device_open(usb_host_client_handle_t, uint8_t, usb_device_handle_t *);
static esp_err_t health_interface_claim(usb_host_client_handle_t, usb_device_handle_t, uint8_t, uint8_t);
static esp_err_t health_device_close(usb_host_client_handle_t, usb_device_handle_t);
static esp_err_t health_interface_release(usb_host_client_handle_t, usb_device_handle_t, uint8_t);
static esp_err_t health_client_deregister(usb_host_client_handle_t);
static esp_err_t health_device_free_all();
static esp_err_t health_host_uninstall();
static esp_err_t health_del_phy(usb_phy_handle_t);
}
#define usb_host_endpoint_halt health_endpoint_halt
#define usb_host_endpoint_flush health_endpoint_flush
#define usb_host_endpoint_clear health_endpoint_clear
#define usb_host_transfer_free health_transfer_free
#define usb_host_device_open health_device_open
#define usb_host_interface_claim health_interface_claim
#define usb_host_device_close health_device_close
#define usb_host_interface_release health_interface_release
#define usb_host_client_deregister health_client_deregister
#define usb_host_device_free_all health_device_free_all
#define usb_host_uninstall health_host_uninstall
#define usb_del_phy health_del_phy
#include "driver.health.body.inc"
#undef usb_host_endpoint_halt
#undef usb_host_endpoint_flush
#undef usb_host_endpoint_clear
#undef usb_host_transfer_free
#undef usb_host_device_open
#undef usb_host_interface_claim
#undef usb_host_device_close
#undef usb_host_interface_release
#undef usb_host_client_deregister
#undef usb_host_device_free_all
#undef usb_host_uninstall
#undef usb_del_phy
namespace {
static esp_err_t health_result(esp_err_t result) {
    if (result != ESP_OK) health_cleanup_failed = true;
    return result;
}
static esp_err_t health_endpoint_halt(usb_device_handle_t d, uint8_t ep) {
    const auto result = usb_host_endpoint_halt(d, ep);
    /* Bulk drain deliberately tolerates a failed halt when flush/completion
     * subsequently returns custody. Interrupt drain rejects it outright. */
    for (const auto &slot : interrupts)
        if (slot.dma && slot.dma->device_handle == d &&
            slot.dma->bEndpointAddress == ep && result != ESP_OK)
            health_cleanup_failed = true;
    return result;
}
static esp_err_t health_endpoint_flush(usb_device_handle_t d, uint8_t ep) {
    if (transfer && transfer->device_handle == d && transfer->bEndpointAddress == ep && inFlight)
        health_bulk_draining = true;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (interrupts[i].dma && interrupts[i].dma->device_handle == d &&
            interrupts[i].dma->bEndpointAddress == ep) health_interrupt_draining[i] = true;
    return health_result(usb_host_endpoint_flush(d, ep));
}
static esp_err_t health_endpoint_clear(usb_device_handle_t d, uint8_t ep) {
    const auto result = usb_host_endpoint_clear(d, ep);
    /* STALL recovery can fail after detach with DMA already returned. The
     * endpoint remains unusable, but this is not failed custody cleanup. */
    if (!inFlight) health_bulk_draining = false;
    return result;
}
static esp_err_t health_transfer_free(usb_transfer_t *dma) {
    const auto result = health_result(usb_host_transfer_free(dma));
    if (result == ESP_OK) {
        if (dma == transfer) health_bulk_draining = false;
        for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
            if (interrupts[i].dma == dma) health_interrupt_draining[i] = false;
    }
    return result;
}
static esp_err_t health_device_open(usb_host_client_handle_t c, uint8_t address, usb_device_handle_t *out) {
    const auto result = usb_host_device_open(c, address, out);
    /* Legacy next_event allocates its token after opening the SDK handle. */
    if (result == ESP_OK && out && *out && serial == UINT64_MAX) health_unassigned_device = *out;
    return result;
}
static esp_err_t health_interface_claim(usb_host_client_handle_t c, usb_device_handle_t d, uint8_t iface, uint8_t alt) {
    const auto result = usb_host_interface_claim(c, d, iface, alt);
    /* Legacy claim_interface likewise allocates identity after SDK admission. */
    if (result == ESP_OK && serial == UINT64_MAX) health_cleanup_failed = true;
    return result;
}
static esp_err_t health_device_close(usb_host_client_handle_t c, usb_device_handle_t d) {
    const auto result = health_result(usb_host_device_close(c, d));
    if (result == ESP_OK && health_unassigned_device == d) health_unassigned_device = nullptr;
    return result;
}
static esp_err_t health_interface_release(usb_host_client_handle_t c, usb_device_handle_t d, uint8_t n) {
    return health_result(usb_host_interface_release(c, d, n));
}
static esp_err_t health_client_deregister(usb_host_client_handle_t c) {
    return health_result(usb_host_client_deregister(c));
}
static esp_err_t health_device_free_all() {
    const auto result = usb_host_device_free_all();
    if (result != ESP_OK && result != ESP_ERR_NOT_FINISHED) health_cleanup_failed = true;
    return result;
}
static esp_err_t health_host_uninstall() { return health_result(usb_host_uninstall()); }
static esp_err_t health_del_phy(usb_phy_handle_t p) { return health_result(usb_del_phy(p)); }
#include "HealthCheck.inc"
}
extern "C" __attribute__((visibility("default")))
const risc_provider_health_v1 risc_provider_health_v1_descriptor = {
    RISC_PROVIDER_HEALTH_API_V1, sizeof(risc_provider_health_v1), provider_health_check
};

namespace {
static risc_usb_vbus_external_api_v1 health_power;
static risc_usb_phy_resource_api_v1 health_phy;
static bool (*health_power_release)(void *, uint64_t);
static bool (*health_power_quiesce)(void *);
static bool (*health_phy_release)(void *, uint64_t);
static bool health_release_power(void *context, uint64_t token) {
    const bool ok = health_power_release(context, token);
    if (!ok) health_cleanup_failed = true;
    return ok;
}
static bool health_quiesce_power(void *context) {
    const bool ok = health_power_quiesce(context);
    if (!ok) health_cleanup_failed = true;
    return ok;
}
static bool health_release_phy(void *context, uint64_t token) {
    const bool ok = health_phy_release(context, token);
    if (!ok) health_cleanup_failed = true;
    return ok;
}
static bool health_start(const risc_provider_dependency_v1 *deps, size_t count) {
    /* Never overwrite an already bound facade on a rejected second start. */
    if (!deps || count != 2 || role.state() != UsbRoleSwitch::State::Off ||
        running || installed || phy || phyRouteCaptured || client || transfer ||
        powerLease || nativePhyLease.held() || fault || ownedBulk.owns_storage() ||
        native_admission_guard()) return start_with_role(deps, count);
    risc_provider_dependency_v1 copy[2];
    for (size_t i = 0; i < count; ++i) {
        copy[i] = deps[i];
        if (equals(deps[i].capability_id, "board.power.vbus") && deps[i].api) {
            const auto *source = static_cast<const risc_usb_vbus_api_v1 *>(deps[i].api);
            if (source->struct_size < sizeof(risc_usb_vbus_monitor_api_v1) ||
                !source->release_host || !source->quiesce) continue;
            const size_t size = source->struct_size >= sizeof(health_power) ?
                sizeof(health_power) : sizeof(health_power.monitor);
            health_power = {};
            std::memcpy(&health_power, source, size);
            health_power.monitor.base.struct_size = size;
            health_power_release = source->release_host;
            health_power_quiesce = source->quiesce;
            health_power.monitor.base.release_host = health_release_power;
            health_power.monitor.base.quiesce = health_quiesce_power;
            copy[i].api = &health_power;
        } else if (equals(deps[i].capability_id, RISC_USB_PHY_RESOURCE_CAPABILITY) && deps[i].api) {
            const auto *source = static_cast<const risc_usb_phy_resource_api_v1 *>(deps[i].api);
            if (!RiscUsbController::NativePhyLease::valid(source)) continue;
            health_phy = *source;
            health_phy.struct_size = sizeof(health_phy);
            health_phy_release = source->release;
            health_phy.release = health_release_phy;
            copy[i].api = &health_phy;
        }
    }
    return start_with_role(copy, count);
}
static const risc_driver_diagnostics_v2 health_driver = {{
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2),
    "usb-controller-esp32s3", "usb.controller", RISC_USB_CONTROLLER_API_V1,
    &hid_interface.base.controller, health_start, stop_with_interrupt,
    []() -> bool { return quiesce_with_interrupt(nullptr); }
}, startup_error};
}
extern "C" __attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &health_driver.base : nullptr;
}
