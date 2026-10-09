#include "RiscProviderV2.h"
#include <string.h>
#ifndef FIXTURE_ID
#error FIXTURE_ID must be set
#endif
#ifndef FIXTURE_CAPABILITY
#error FIXTURE_CAPABILITY must be set
#endif
static int value = 42;
static bool started;
#ifdef FIXTURE_QUIESCE_FAIL_ONCE
#ifndef FIXTURE_QUIESCE_FAILURES
#define FIXTURE_QUIESCE_FAILURES 1
#endif
static unsigned quiesce_failures = FIXTURE_QUIESCE_FAILURES;
#endif
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started) return false;
#ifdef FIXTURE_REQUIRE
    if (!deps || count != 1 || !deps[0].capability_id ||
        strcmp(deps[0].capability_id, FIXTURE_REQUIRE) != 0 ||
        deps[0].api_version != 1 || !deps[0].api) return false;
#else
    if (deps || count) return false;
#endif
    started = true;
    return true;
}
static bool quiesce(void) {
#ifdef FIXTURE_QUIESCE_FAIL
    return !started;
#elif defined(FIXTURE_QUIESCE_FAIL_ONCE)
    if (started && quiesce_failures) { --quiesce_failures; return false; }
    return true;
#else
    return true;
#endif
}
static void stop(void) { started = false; }
#ifdef FIXTURE_DIAGNOSTICS
static bool last_error(char *out, size_t capacity) {
    static const char text[] = "fixture diagnostic";
    if (!out || !capacity) return false;
    ++value;
    const size_t n = sizeof(text) < capacity ? sizeof(text) : capacity;
    memcpy(out, text, n);
    // Deliberately omit NUL on truncation: the generic copied-diagnostic
    // boundary must terminate within the caller's capacity before logging.
    return true;
}
static const risc_driver_diagnostics_v2 driver = {
  { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver),
    FIXTURE_ID, FIXTURE_CAPABILITY, 1, &value, start, stop, quiesce }, last_error
};
#else
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    FIXTURE_ID, FIXTURE_CAPABILITY, 1, &value, start, stop, quiesce
};
#endif
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? (const risc_driver_v2 *)&driver : 0;
}
