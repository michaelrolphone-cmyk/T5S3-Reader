#include "RiscProviderV2.h"
#include "RiscNetworkLinkV1.h"
#include <string.h>
#ifndef FIXTURE_ID
#define FIXTURE_ID "test.network"
#endif
#ifndef FIXTURE_CAPABILITY
#define FIXTURE_CAPABILITY RISC_NETWORK_LINK_CAPABILITY
#endif

/* Host ELF only: real generic graph lifecycle, no SDK or physical radio. */
static bool started, failed_end, failed_quiesce;
static uint64_t next_token, tokens[4];
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || deps || count) return false;
    started = true;
#ifdef FIXTURE_FAIL_START
    return false;
#else
    return true;
#endif
}
static int32_t begin_use(void *context, uint32_t flags, uint64_t *token) {
    (void)context;
    if (!started || !token || flags != RISC_NETWORK_LINK_LOW_LATENCY) return RISC_NETWORK_LINK_REJECTED;
    *token = 0;
    for (unsigned i = 0; i != 4; ++i) {
        if (tokens[i]) continue;
        *token = tokens[i] = ++next_token;
        return RISC_NETWORK_LINK_READY;
    }
    return RISC_NETWORK_LINK_REJECTED;
}
static bool state(void *context, uint64_t token, risc_network_link_state_v1 *out) {
    (void)context;
    bool found = token == 0;
    for (unsigned i = 0; i != 4; ++i) if (tokens[i] == token) found = true;
    if (!started || !found || !out || out->struct_size != sizeof(*out)) return false;
    out->connected = out->has_address = 1;
    strcpy(out->interface_name, "ifA");
    strcpy(out->address, "192.0.2.2");
    return true;
}
static int32_t end_use(void *context, uint64_t token) {
    (void)context;
    for (unsigned i = 0; i != 4; ++i) {
        if (!token || tokens[i] != token) continue;
        if (!failed_end) { failed_end = true; return RISC_NETWORK_LINK_RETAINED; }
        tokens[i] = 0;
        return RISC_NETWORK_LINK_READY;
    }
    return RISC_NETWORK_LINK_REJECTED;
}
static bool quiesce(void) {
#ifdef FIXTURE_FAIL_START
    static unsigned failures = 3;
    if (failures) { --failures; return false; }
#endif
    for (unsigned i = 0; i != 4; ++i) if (tokens[i]) return false;
    if (!failed_quiesce) { failed_quiesce = true;
#ifndef FIXTURE_FAIL_START
      return false;
#endif
    }
    return true;
}
static void stop(void) { started = false; }
static const risc_network_link_v1 api = {
    RISC_NETWORK_LINK_API_V1, sizeof(api), 0, begin_use, state, end_use
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), FIXTURE_ID,
    FIXTURE_CAPABILITY, RISC_NETWORK_LINK_API_V1, &api,
    start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
