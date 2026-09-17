/* Test-only provider: start acquires a simulated resource then fails; the
 * dependency's mutable value models an external hardware fault being fixed.
 * Do not publish as a driver or treat this as physical hardware acceptance. */
#include "RiscProviderV2.h"
#include <string.h>
static const int *recovery_gate;
static bool hardware_held;
static int output = 99;
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (hardware_held || !deps || count != 1 || !deps[0].capability_id ||
        strcmp(deps[0].capability_id, "cap.root") ||
        deps[0].api_version != 1 || !deps[0].api) return false;
    recovery_gate = (const int *)deps[0].api;
    hardware_held = true; /* Side effect BEFORE reporting start failure. */
    return false;
}
static bool quiesce(void) {
    return !hardware_held || (recovery_gate && *recovery_gate == 43);
}
static void stop(void) {
    if (!quiesce()) return; /* Loader must never reach this on failed check. */
    hardware_held = false;
    recovery_gate = 0;
}
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "fixture-failed-start", "cap.failed-start", 1, &output,
    start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
