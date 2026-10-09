/* Explicit health profile only; the default translation unit is unchanged. */
#define t5_driver_get provider_legacy_get
#include "driver.c"
#undef t5_driver_get
#include "RiscProviderHealthV1.h"

static bool health_cleanup_failed;
static int32_t provider_health_check(void) {
    if (health_cleanup_failed) return RISC_PROVIDER_HEALTH_RETAINED;
    if (deadline_retained) return RISC_PROVIDER_HEALTH_RETAINED;
    /* A legacy false release gives no typed settled/pending guarantee. Raw
     * HID and XInput can hide this behind their void release compatibility. */
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (claims[i].token && claims[i].closing) return RISC_PROVIDER_HEALTH_RETAINED;
    if (event_fault) return RISC_PROVIDER_HEALTH_UNKNOWN;
    return RISC_PROVIDER_HEALTH_READY;
}
__attribute__((visibility("default")))
const risc_provider_health_v1 risc_provider_health_v1_descriptor = {
    RISC_PROVIDER_HEALTH_API_V1, sizeof(risc_provider_health_v1), provider_health_check
};

static bool health_release_checked(void *context, uint64_t token) {
    const bool ok = release_checked(context, token);
    if (!ok) for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (claims[i].token == token && claims[i].closing) health_cleanup_failed = true;
    return ok;
}
static void health_release(void *context, uint64_t token) {
    (void)health_release_checked(context, token);
}
static bool health_start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (!start(deps, count)) return false;
    interface.snapshot.discovery.host.release = health_release;
    interface.snapshot.discovery.release_checked = health_release_checked;
    return true;
}
static bool health_live_claim(void) {
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (claims[i].token && !claims[i].closing) return true;
    return false;
}
static bool health_quiesce(void) {
    const bool live = health_live_claim();
    const bool ok = quiesce();
    if (!ok && !live) health_cleanup_failed = true;
    return ok;
}
static void health_stop(void) {
    const bool live = health_live_claim();
    stop();
    if (controller && !live) health_cleanup_failed = true;
}
static const risc_driver_v2 health_driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-host-v2", "usb.host", RISC_USB_HOST_API_V1,
    &interface.snapshot.discovery.host, health_start, health_stop, health_quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &health_driver : 0;
}
