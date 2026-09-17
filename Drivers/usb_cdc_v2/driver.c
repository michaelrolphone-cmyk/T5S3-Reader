#include "RiscUsbProviderV1.h"

/* Unlike the legacy descriptor-only ELF, this provider owns the entire CDC
 * class lifecycle. ALL USB transactions use usb.host implemented by another
 * ELF; none are forwarded to RiscRTE firmware or a kernel.usb service. */
typedef struct {
    uint64_t token;
    uint64_t device;
    uint64_t control_claim;
    uint64_t data_claim;
    uint8_t control_interface;
    uint8_t data_interface;
    uint8_t data_alternate;
    uint8_t ep_in;
    uint8_t ep_out;
} cdc_session;

static const risc_usb_host_api_v1 *host;
static cdc_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t config_bytes[RISC_USB_CONFIG_LIMIT];
static uint64_t generation;

static bool same(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static cdc_session *lookup(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}

static void release_session(cdc_session *s) {
    if (host && s->data_claim && s->data_claim != s->control_claim)
        host->release(host->context, s->data_claim);
    if (host && s->control_claim)
        host->release(host->context, s->control_claim);
    s->token = s->device = s->control_claim = s->data_claim = 0;
    s->control_interface = s->data_interface = s->data_alternate = 0;
    s->ep_in = s->ep_out = 0;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || !deps || count != 1 || !same(deps[0].capability_id, "usb.host") ||
        deps[0].api_version != RISC_USB_HOST_API_V1 || !deps[0].api) return false;
    const risc_usb_host_api_v1 *api = (const risc_usb_host_api_v1 *)deps[0].api;
    if (api->api_version != RISC_USB_HOST_API_V1 ||
        api->struct_size < sizeof(*api) || !api->configuration || !api->claim ||
        !api->release || !api->control || !api->bulk_read || !api->bulk_write)
        return false;
    host = api;
    return true;
}

static void stop(void) {
    if (!host) return;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        release_session(&sessions[i]);
    host = 0;
}

/* Deliberately accepts exactly one unambiguous CDC control/data pair. A
 * composite device with multiple pairs needs IAD/union selection in v2;
 * silently binding the wrong data interface would corrupt another device. */
static bool parse(size_t length, cdc_session *out) {
    if (length < 9 || length > RISC_USB_CONFIG_LIMIT ||
        config_bytes[0] < 9 || config_bytes[1] != 2) return false;
    size_t total = (size_t)config_bytes[2] | ((size_t)config_bytes[3] << 8);
    if (total < 9 || total > length) return false;
    uint8_t ctl = 0xff, data = 0xff, alternate = 0;
    uint8_t current_class = 0xff, current_interface = 0xff;
    uint8_t ep_in = 0, ep_out = 0;
    for (size_t at = 0; at < total;) {
        if (total - at < 2) return false;
        uint8_t n = config_bytes[at], type = config_bytes[at + 1];
        if (n < 2 || n > total - at) return false;
        if (type == 4 && n >= 9) {
            current_interface = config_bytes[at + 2];
            uint8_t alt = config_bytes[at + 3];
            current_class = config_bytes[at + 5];
            if (current_class == 2 && alt == 0) {
                if (ctl != 0xff && ctl != current_interface) return false;
                ctl = current_interface;
            }
            if (current_class == 10 && alt == 0) {
                if (data != 0xff && data != current_interface) return false;
                data = current_interface;
                alternate = alt;
            }
        } else if (type == 5 && n >= 7 && current_class == 10 &&
                   current_interface == data) {
            uint8_t ep = config_bytes[at + 2];
            uint8_t kind = config_bytes[at + 3] & 3u;
            uint16_t mps = (uint16_t)config_bytes[at + 4] |
                           ((uint16_t)config_bytes[at + 5] << 8);
            if (kind == 2 && mps && mps <= 512) {
                if (ep & 0x80u) {
                    if (ep_in) return false;
                    ep_in = ep;
                } else {
                    if (ep_out) return false;
                    ep_out = ep;
                }
            }
        }
        at += n;
    }
    if (ctl == 0xff || data == 0xff || ctl == data || !ep_in || !ep_out)
        return false;
    out->control_interface = ctl;
    out->data_interface = data;
    out->data_alternate = alternate;
    out->ep_in = ep_in;
    out->ep_out = ep_out;
    return true;
}

static uint64_t open_device(uint64_t device) {
    if (!host || !device) return 0;
    cdc_session *slot = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) return 0;
    size_t length = sizeof(config_bytes);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, config_bytes, &length,
                             &vid, &pid)) return 0;
    (void)vid; (void)pid; /* CDC matches descriptors, not a vendor list. */
    cdc_session candidate = {0};
    if (!parse(length, &candidate)) return 0;
    candidate.device = device;
    if (!host->claim(host->context, device, candidate.control_interface, 0,
                     &candidate.control_claim) || !candidate.control_claim)
        return 0;
    if (!host->claim(host->context, device, candidate.data_interface,
                     candidate.data_alternate, &candidate.data_claim) ||
        !candidate.data_claim) {
        host->release(host->context, candidate.control_claim);
        return 0;
    }
    ++generation;
    if (!generation) ++generation;
    candidate.token = generation;
    *slot = candidate;
    return candidate.token;
}

static bool configure(uint64_t token, uint32_t baud, uint8_t bits,
                      uint8_t parity, uint8_t stop_bits) {
    cdc_session *s = lookup(token);
    if (!s || baud < 300 || baud > 3000000 || bits < 5 || bits > 8 ||
        parity > 4 || (stop_bits != 1 && stop_bits != 2)) return false;
    uint8_t payload[7] = {(uint8_t)baud, (uint8_t)(baud >> 8),
        (uint8_t)(baud >> 16), (uint8_t)(baud >> 24),
        stop_bits == 2 ? 2u : 0u, parity, bits};
    return host->control(host->context, s->device, 0x21, 0x20, 0,
                         s->control_interface, payload, 7, 1000) == 7;
}

static bool control_lines(uint64_t token, bool dtr, bool rts) {
    cdc_session *s = lookup(token);
    if (!s) return false;
    uint16_t value = (uint16_t)((dtr ? 1u : 0u) | (rts ? 2u : 0u));
    return host->control(host->context, s->device, 0x21, 0x22, value,
                         s->control_interface, 0, 0, 1000) == 0;
}

static int32_t read_data(uint64_t token, uint8_t *dst, size_t capacity,
                         uint32_t timeout_ms) {
    cdc_session *s = lookup(token);
    if (!s || !dst || !capacity || capacity > RISC_USB_CONFIG_LIMIT) return -1;
    return host->bulk_read(host->context, s->data_claim, s->ep_in,
                           dst, capacity, timeout_ms);
}

static int32_t write_data(uint64_t token, const uint8_t *src, size_t length,
                          uint32_t timeout_ms) {
    cdc_session *s = lookup(token);
    if (!s || !src || !length || length > RISC_USB_CONFIG_LIMIT) return -1;
    return host->bulk_write(host->context, s->data_claim, s->ep_out,
                            src, length, timeout_ms);
}

static bool close_device(uint64_t token) {
    cdc_session *s = lookup(token);
    if (!s) return false;
    release_session(s);
    return true;
}

static const risc_usb_cdc_api_v1 capability = {
    RISC_USB_CDC_API_V1, sizeof(risc_usb_cdc_api_v1),
    open_device, configure, control_lines, read_data, write_data, close_device
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-cdc-acm-v2", "serial.port", RISC_USB_CDC_API_V1,
    &capability, start, stop
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
