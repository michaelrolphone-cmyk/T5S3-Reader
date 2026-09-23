#include "RiscUsbControllerV1.h"
#include <stddef.h>
#include <stdint.h>

/* U1 installable witness class. It intentionally matches only the simulated
 * 0xCAFE:0x4001 fixture identity used by regression tests. The package proves
 * that adding a fourth serial class requires only an installed manifest/ELF,
 * not a firmware class table or dispatcher change. It is not commercial
 * hardware support and remains unpublished. */
typedef struct {
    uint64_t token;
    uint64_t device;
    uint64_t claim;
    uint8_t iface;
    uint8_t alt;
    uint8_t ep_in;
    uint8_t ep_out;
    uint32_t rx, tx;
    uint8_t rx_pending[512], tx_pending[512];
    uint32_t rx_size, rx_offset, tx_size, tx_offset;
    bool failed;
} witness_session;

static const risc_usb_host_api_v1 *host;
static const risc_usb_host_discovery_v1 *discovery;
static witness_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t descriptor[RISC_USB_CONFIG_LIMIT];
static uint64_t sequence;
static const risc_stream_provider_v1 *streams;
static size_t next_session;
static void fail_endpoints(witness_session *, int32_t);
static int32_t read_data(uint64_t, uint8_t *, size_t, uint32_t);
static int32_t write_data(uint64_t, const uint8_t *, size_t, uint32_t);
static bool bind_streams(const risc_stream_provider_v1 *api) {
    if (host || streams || !api || api->api_version != RISC_STREAM_PROVIDER_API_V1 ||
        api->struct_size < sizeof(*api) || !api->context || !api->publish ||
        !api->produce || !api->consume || !api->finish || !api->close) return false;
    streams = api;
    return true;
}
static void close_endpoints(witness_session *s) {
    s->failed = true; // Stop data work before any physical close can fail.
    if (streams && s->rx) (void)streams->close(streams->context, s->rx);
    if (streams && s->tx) (void)streams->close(streams->context, s->tx);
    s->rx = s->tx = 0;
    s->rx_size = s->rx_offset = s->tx_size = s->tx_offset = 0;
}

static bool same(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool supported(uint16_t vid, uint16_t pid) {
    return vid == 0xcafeu && pid == 0x4001u;
}
static witness_session *lookup(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}
static bool release_session(witness_session *s) {
    if (!host || !discovery || !s || !s->claim ||
        !discovery->release_checked(host->context, s->claim)) return false;
    *s = (witness_session){0};
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || !deps || count != 1 ||
        !same(deps[0].capability_id, "usb.host") ||
        deps[0].api_version != RISC_USB_HOST_API_V1 || !deps[0].api)
        return false;
    const risc_usb_host_api_v1 *api =
        (const risc_usb_host_api_v1 *)deps[0].api;
    if (api->api_version != RISC_USB_HOST_API_V1 ||
        api->struct_size < sizeof(risc_usb_host_discovery_v1) ||
        !api->configuration || !api->claim || !api->bulk_read ||
        !api->bulk_write) return false;
    const risc_usb_host_discovery_v1 *extended =
        (const risc_usb_host_discovery_v1 *)api;
    if (!extended->poll || !extended->devices ||
        !extended->release_checked || !extended->control_claim)
        return false;
    host = api;
    discovery = extended;
    return true;
}
static bool quiesce(void) {
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token || sessions[i].claim) return false;
    return true;
}
static void stop(void) {
    if (!quiesce()) return;
    discovery = 0;
    host = 0;
    streams = 0;
    next_session = 0;
}

static bool parse(size_t length, witness_session *result) {
    if (!result || length < 9 || length > sizeof(descriptor) ||
        descriptor[0] < 9 || descriptor[1] != 2 ||
        ((size_t)descriptor[2] | ((size_t)descriptor[3] << 8)) != length)
        return false;
    uint8_t iface = 0xff, alt = 0, cls = 0;
    uint8_t in_ep = 0, out_ep = 0;
    size_t candidates = 0;
    witness_session found = {0};
    for (size_t pos = 0; pos < length;) {
        if (length - pos < 2) return false;
        const uint8_t n = descriptor[pos], kind = descriptor[pos + 1];
        if (n < 2 || n > length - pos) return false;
        if (kind == 4) {
            if (cls == 0xff && iface != 0xff && in_ep && out_ep) {
                ++candidates;
                found.iface = iface; found.alt = alt;
                found.ep_in = in_ep; found.ep_out = out_ep;
            }
            if (n < 9) return false;
            iface = descriptor[pos + 2];
            alt = descriptor[pos + 3];
            cls = descriptor[pos + 5];
            in_ep = out_ep = 0;
        } else if (kind == 5 && cls == 0xff && iface != 0xff) {
            if (n < 7 || (descriptor[pos + 3] & 3u) != 2u) return false;
            const uint16_t mps = (uint16_t)descriptor[pos + 4] |
                                 ((uint16_t)descriptor[pos + 5] << 8);
            const uint8_t ep = descriptor[pos + 2];
            if (!mps || mps > 512 || !(ep & 0x0fu) || (ep & 0x70u))
                return false;
            if (ep & 0x80u) {
                if (in_ep) return false;
                in_ep = ep;
            } else {
                if (out_ep) return false;
                out_ep = ep;
            }
        }
        pos += n;
    }
    if (cls == 0xff && iface != 0xff && in_ep && out_ep) {
        ++candidates;
        found.iface = iface; found.alt = alt;
        found.ep_in = in_ep; found.ep_out = out_ep;
    }
    if (candidates != 1) return false;
    *result = found;
    return true;
}

static int32_t probe_device(uint64_t device) {
    if (!host || !device) return -1;
    size_t length = sizeof(descriptor);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, descriptor, &length,
                             &vid, &pid)) return -1;
    if (!supported(vid, pid)) return 0;
    witness_session candidate = {0};
    return parse(length, &candidate) ? 1 : 0;
}

static bool snapshot_devices(risc_serial_device_v1 *out, size_t *inout_count) {
    if (!host || !discovery || !inout_count) return false;
    size_t processed = 0;
    if (!discovery->poll(host->context, 16, &processed)) return false;
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    if (!discovery->devices(host->context, tokens, &count) ||
        count > RISC_USB_HOST_MAX_DEVICES) return false;
    risc_serial_device_v1 found[RISC_USB_HOST_MAX_DEVICES] = {{0}};
    size_t matches = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!tokens[i]) return false;
        const int32_t decision = probe_device(tokens[i]);
        if (decision < 0) return false;
        if (!decision) continue;
        found[matches].provider_device = tokens[i];
        found[matches].generation = tokens[i];
        found[matches].transport = RISC_SERIAL_TRANSPORT_USB;
        ++matches;
    }
    if (*inout_count < matches || (matches && !out)) {
        *inout_count = matches;
        return false;
    }
    // A successful inventory is authoritative for disconnect. Revoke copied
    // endpoints immediately; physical claim cleanup remains checked close.
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        witness_session *s = &sessions[i];
        if (!s->token || !s->rx || !s->tx) continue;
        bool present = false;
        for (size_t j = 0; j < count; ++j) present |= tokens[j] == s->device;
        if (!present) {
            fail_endpoints(s, -6);
            close_endpoints(s);
        }
    }
    for (size_t i = 0; i < matches; ++i) out[i] = found[i];
    *inout_count = matches;
    return true;
}

static uint64_t open_device(uint64_t device) {
    if (!host || !discovery || !device || sequence == UINT64_MAX) return 0;
    witness_session *slot = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (!sessions[i].token && !sessions[i].claim) { slot = &sessions[i]; break; }
    if (!slot) return 0;
    size_t length = sizeof(descriptor);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, descriptor, &length,
                             &vid, &pid) || !supported(vid, pid))
        return 0;
    witness_session candidate = {0};
    if (!parse(length, &candidate)) return 0;
    candidate.device = device;
    if (!host->claim(host->context, device, candidate.iface, candidate.alt,
                     &candidate.claim) || !candidate.claim)
        return 0;
    candidate.token = ++sequence;
    *slot = candidate;
    return candidate.token;
}

static bool configure(uint64_t token, uint32_t baud, uint8_t bits,
                      uint8_t parity, uint8_t stops) {
    witness_session *s = lookup(token);
    if (!s || baud < 300u || baud > 3000000u ||
        bits < 5u || bits > 8u || parity > 4u ||
        (stops != 1u && stops != 2u)) return false;
    uint8_t payload[7] = {
        (uint8_t)baud, (uint8_t)(baud >> 8), (uint8_t)(baud >> 16),
        (uint8_t)(baud >> 24), bits, parity, stops
    };
    return discovery->control_claim(host->context, s->claim, 0x41u, 0x30u,
                                    0, s->iface, payload, sizeof(payload),
                                    1000) == (int32_t)sizeof(payload);
}
static bool control_lines(uint64_t token, bool dtr, bool rts) {
    witness_session *s = lookup(token);
    if (!s) return false;
    const uint16_t value =
        (uint16_t)((dtr ? 1u : 0u) | (rts ? 2u : 0u));
    return discovery->control_claim(host->context, s->claim, 0x41u, 0x31u,
                                    value, s->iface, 0, 0, 1000) == 0;
}
static int32_t read_data(uint64_t token, uint8_t *dst, size_t capacity,
                         uint32_t timeout_ms) {
    witness_session *s = lookup(token);
    if (!s || !dst || !capacity || capacity > RISC_USB_CONFIG_LIMIT) return -1;
    const int32_t n = host->bulk_read(host->context, s->claim, s->ep_in,
                                      dst, capacity, timeout_ms);
    return n >= 0 && (size_t)n <= capacity ? n : -1;
}
static int32_t write_data(uint64_t token, const uint8_t *src, size_t length,
                          uint32_t timeout_ms) {
    witness_session *s = lookup(token);
    if (!s || !src || !length || length > RISC_USB_CONFIG_LIMIT) return -1;
    const int32_t n = host->bulk_write(host->context, s->claim, s->ep_out,
                                       src, length, timeout_ms);
    return n >= 0 && (size_t)n <= length ? n : -1;
}
static bool close_device(uint64_t token) {
    witness_session *s = lookup(token);
    if (!s) return false;
    close_endpoints(s);
    return release_session(s);
}
static bool endpoints(uint64_t token, uint32_t *rx, uint32_t *tx) {
    if (rx) *rx = 0;
    if (tx) *tx = 0;
    witness_session *s = lookup(token);
    if (!streams || !s || s->failed || !rx || !tx) return false;
    if (!s->rx && !s->tx) {
        const risc_stream_endpoint_v1 source = {sizeof(source), 1, 1, 2048, 0, 0, 0};
        const risc_stream_endpoint_v1 sink = {sizeof(sink), 1, 2, 2048, 0, 0, 0};
        if (streams->publish(streams->context, &source, &s->rx) != 0 ||
            streams->publish(streams->context, &sink, &s->tx) != 0) {
            close_endpoints(s);
            return false;
        }
    }
    if (!s->rx || !s->tx) return false;
    *rx = s->rx; *tx = s->tx;
    return true;
}
static void fail_endpoints(witness_session *s, int32_t error) {
    s->failed = true;
    (void)streams->finish(streams->context, s->rx, error);
    (void)streams->finish(streams->context, s->tx, error);
}
static bool flush_rx(witness_session *s) {
    if (s->rx_size == s->rx_offset) return true;
    uint32_t accepted = 0;
    const uint32_t remaining = s->rx_size - s->rx_offset;
    const int32_t result = streams->produce(streams->context, s->rx,
        s->rx_pending + s->rx_offset, remaining, &accepted);
    if (result < 0 || result > 1 || accepted > remaining) {
        fail_endpoints(s, -5);
        return false;
    }
    s->rx_offset += accepted;
    if (s->rx_offset == s->rx_size) s->rx_offset = s->rx_size = 0;
    return true;
}
static void poll_streams(uint32_t budget_ms) {
    if (!streams || !host || budget_ms < 2) return;
    // One session per turn, <= one 512-byte read and write, each <=1 ms.
    // The generic dispatcher enforces item/time checkpoints and yields.
    for (size_t visited = 0; visited < RISC_USB_CDC_MAX_SESSIONS; ++visited) {
        witness_session *s = &sessions[next_session];
        next_session = (next_session + 1) % RISC_USB_CDC_MAX_SESSIONS;
        if (!s->token || !s->rx || !s->tx || s->failed) continue;
        if (!flush_rx(s)) return;
        if (!s->rx_size) {
            const int32_t n = read_data(s->token, s->rx_pending, sizeof(s->rx_pending), 1);
            if (n < 0) { fail_endpoints(s, -5); return; }
            s->rx_size = (uint32_t)n;
            if (!flush_rx(s)) return;
        }
        if (s->tx_size == s->tx_offset) {
            uint32_t n = 0;
            const int32_t result = streams->consume(streams->context, s->tx,
                s->tx_pending, sizeof(s->tx_pending), &n);
            if (result < 0 || result > 2 || n > sizeof(s->tx_pending)) {
                fail_endpoints(s, -5); return;
            }
            s->tx_size = n; s->tx_offset = 0;
        }
        if (s->tx_size > s->tx_offset) {
            const uint32_t remaining = s->tx_size - s->tx_offset;
            const int32_t n = write_data(s->token, s->tx_pending + s->tx_offset, remaining, 1);
            if (n < 0) { fail_endpoints(s, -5); return; }
            s->tx_offset += (uint32_t)n;
            if (s->tx_offset == s->tx_size) s->tx_offset = s->tx_size = 0;
        }
        return;
    }
}
// Raw capability calls cannot bypass an activated endpoint pair's ordering.
static int32_t legacy_read(uint64_t token, uint8_t *data, size_t n, uint32_t ms) {
    witness_session *s = lookup(token);
    return s && !s->rx && !s->failed ? read_data(token, data, n, ms) : -1;
}
static int32_t legacy_write(uint64_t token, const uint8_t *data, size_t n, uint32_t ms) {
    witness_session *s = lookup(token);
    return s && !s->tx && !s->failed ? write_data(token, data, n, ms) : -1;
}

static const risc_serial_port_streams_v1 capability = {
    {{{RISC_USB_CDC_API_V1, sizeof(risc_serial_port_streams_v1),
       open_device, configure, control_lines, legacy_read, legacy_write, close_device},
      probe_device}, snapshot_devices}, endpoints
};
static const risc_driver_poll_v2 driver = {
    {{RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_poll_v2),
      "usb-serial-witness", "serial.port", RISC_USB_CDC_API_V1,
      &capability.inventory.discovery.serial, start, stop, quiesce}, bind_streams},
    poll_streams
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.streams.driver : 0;
}
