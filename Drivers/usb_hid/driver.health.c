/* Explicit health profile only; the default translation unit is unchanged. */
#include "driver.c"
#include "RiscProviderHealthV1.h"

static int32_t provider_health_check(void) {
    /* Session records contain copied identities and opaque claims only.
     * close() success is NOT lower-host health; the graph checks that ELF. */
    return fault ? RISC_PROVIDER_HEALTH_UNKNOWN : RISC_PROVIDER_HEALTH_READY;
}
__attribute__((visibility("default")))
const risc_provider_health_v1 risc_provider_health_v1_descriptor = {
    RISC_PROVIDER_HEALTH_API_V1, sizeof(risc_provider_health_v1), provider_health_check
};
