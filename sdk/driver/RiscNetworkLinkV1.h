#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Provider-neutral link use, not Wi-Fi administration or a socket API.
 * The provider owns radio/link and power policy. A use borrows an already
 * configured host-network link; it never selects a device or joins a network.
 * Calls run on the serialized provider owner task and retain no caller pointers.
 * The provider's generic quiesce must also drain every outstanding use. */
#define RISC_NETWORK_LINK_CAPABILITY "network.default"
#define RISC_NETWORK_LINK_API_V1 1u
#define RISC_NETWORK_LINK_READY 0
#define RISC_NETWORK_LINK_PENDING 1
#define RISC_NETWORK_LINK_RETAINED 2
#define RISC_NETWORK_LINK_REJECTED (-1)
#define RISC_NETWORK_LINK_LOW_LATENCY 1u

typedef struct {
    uint32_t struct_size;
    uint8_t connected, has_address;
    uint8_t reserved[2];
    char address[48];
} risc_network_link_state_v1;

typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    /* LOW_LATENCY is an activity request, not an ESP power-save setting.
     * Reject unsupported flags. READY requires a nonzero non-reused token.
     * Any partially admitted use returns its token even on failure; the
     * consumer retains it and calls end_use, never retries begin_use over it.
     * A zero token promises that no consumer activity remains. */
    int32_t (*begin_use)(void *, uint32_t flags, uint64_t *token);
    /* Copied observation; token zero observes the link without creating use. */
    bool (*state)(void *, uint64_t token, risc_network_link_state_v1 *out);
    /* READY means this exact use is drained, not that every link user is idle.
     * Every other result retains the same token for bounded explicit retry.
     * No callbacks, app buffers or new work may outlive a successful end. */
    int32_t (*end_use)(void *, uint64_t token);
} risc_network_link_v1;

#define RISC_NETWORK_LINK_V1_SIZE \
    (offsetof(risc_network_link_v1, end_use) + sizeof(((risc_network_link_v1 *)0)->end_use))
