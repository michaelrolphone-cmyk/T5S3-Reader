/* Explicit health profile only; the default translation unit is unchanged. */
#define t5_driver_get provider_legacy_get
#include "driver.c"
#undef t5_driver_get
#include "RiscProviderHealthV1.h"

static bool health_cleanup_failed;
static int32_t provider_health_check(void) {
    if (health_cleanup_failed) return RISC_PROVIDER_HEALTH_RETAINED;
    for (size_t i = 0; i < RISC_USB_HID_MAX_INTERFACES; ++i)
        if (inspected[i].closing) return RISC_PROVIDER_HEALTH_RETAINED;
    /* Report/layout/state/mailbox memory is owned by this ELF. Dependency
     * mappings, including raw HID's actual claims, are checked separately. */
    return RISC_PROVIDER_HEALTH_READY;
}
__attribute__((visibility("default")))
const risc_provider_health_v1 risc_provider_health_v1_descriptor = {
    RISC_PROVIDER_HEALTH_API_V1, sizeof(risc_provider_health_v1), provider_health_check
};

/* The selected profile observes the actual false cleanup boundary. The
 * facade owns its table and copies only dependency-pinned function pointers;
 * no dependency array or invoking caller memory survives start(). */
static risc_usb_hid_api_v1 health_dependency;
static bool (*health_original_cleanup)(void *, uint64_t);
static bool health_cleanup(void *context, uint64_t token) {
    const bool ok = health_original_cleanup(context, token);
    if (!ok) health_cleanup_failed = true;
    return ok;
}
static bool health_start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (hid || !deps || count > 2) return start(deps, count);
    risc_provider_dependency_v1 copy[2];
    for (size_t i = 0; i < count; ++i) {
        copy[i] = deps[i];
        if (!equal(deps[i].capability_id, "usb.hid") || !deps[i].api) continue;
        const risc_usb_hid_api_v1 *source = deps[i].api;
        if (source->struct_size < sizeof(*source) || !source->close) continue;
        health_dependency = *source;
        health_dependency.struct_size = sizeof(health_dependency);
        health_original_cleanup = source->close;
        health_dependency.close = health_cleanup;
        copy[i].api = &health_dependency;
    }
    return start(copy, count);
}
static const risc_driver_v2 health_driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-hid-gamepad", "usb.hid.gamepad", 1,
    &api, health_start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &health_driver : 0;
}
