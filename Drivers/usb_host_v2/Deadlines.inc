/* Included only by the physical host arbitration ELF. No fallback to a legacy
 * controller callback can satisfy this contract. Legacy admission is separate. */
typedef struct { uint64_t began; uint32_t budget; } host_deadline;

static uint64_t deadline_now(void *ctx) {
    (void)ctx;
    if (!timed_controller || !controller) return last_clock;
    uint64_t now = timed_controller->now_ms(controller->controller.context);
    if (now < last_clock) { deadline_retained = true; return last_clock; }
    last_clock = now;
    return now;
}
static int32_t deadline_begin(host_deadline *d, uint32_t budget) {
    if (!controller || !timed_controller) return RISC_STREAM_UNSUPPORTED;
    if (deadline_retained) return RISC_STREAM_RETAINED;
    if (!budget || budget > RISC_USB_DEADLINE_MAX_MS) return RISC_STREAM_INVALID;
    d->began = deadline_now(0);
    d->budget = budget;
    return deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_OK;
}
static uint32_t deadline_left(const host_deadline *d) {
    uint64_t now = deadline_now(0);
    if (deadline_retained || now < d->began || now - d->began >= d->budget)
        return 0;
    return d->budget - (uint32_t)(now - d->began);
}
/* An observed overrun is evidence to fence a broken lower contract, never a
 * technique for turning an unbounded SDK call into a bounded operation. */
static int32_t deadline_result(const host_deadline *d, int32_t rc) {
    uint64_t now = deadline_now(0);
    if (now < d->began || now - d->began > d->budget ||
        rc == RISC_STREAM_RETAINED) deadline_retained = true;
    return deadline_retained ? RISC_STREAM_RETAINED : rc;
}
static int32_t deadline_inventory(const host_deadline *d) {
    if (event_fault) return RISC_STREAM_IO;
    for (size_t n = 0; n < 16; ++n) {
        uint32_t left = deadline_left(d);
        if (!left) return deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT;
        risc_usb_controller_event_v1 event = {0};
        int32_t rc = timed_controller->next_event(controller->controller.context,
                                                 &event, left);
        rc = deadline_result(d, rc);
        if (rc < 0) return rc;
        if (!rc) return deadline_left(d) ? RISC_STREAM_OK :
            (deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT);
        if (rc != 1 || !apply_event(&event)) {
            event_fault = true;
            return RISC_STREAM_IO;
        }
    }
    /* We consumed the work limit without observing empty. The inventory may
     * be partial; retain it internally and never publish it as complete. */
    return RISC_STREAM_IO;
}
static int32_t deadline_snapshot(void *ctx, uint64_t *out, size_t *count,
                                 uint32_t budget) {
    (void)ctx;
    if (!count || (*count && !out)) return RISC_STREAM_INVALID;
    host_deadline d;
    int32_t rc = deadline_begin(&d, budget);
    if (rc != RISC_STREAM_OK) return rc;
    rc = deadline_inventory(&d);
    if (rc != RISC_STREAM_OK) return rc;
    size_t actual = 0;
    uint64_t copy[RISC_USB_HOST_MAX_DEVICES];
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
        if (devices[i].present) copy[actual++] = devices[i].token;
    if (*count < actual) { *count = actual; return RISC_STREAM_LIMIT; }
    if (!deadline_left(&d)) return deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT;
    for (size_t i = 0; i < actual; ++i) out[i] = copy[i];
    *count = actual;
    return RISC_STREAM_OK;
}
static int32_t deadline_config(const host_deadline *d, uint64_t token,
                               uint8_t *out, size_t *length,
                               uint16_t *vid, uint16_t *pid) {
    device_slot *device = device_for(token);
    if (!device) return RISC_STREAM_DISCONNECTED;
    uint32_t left = deadline_left(d);
    if (!left) return deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT;
    const size_t capacity = *length;
    int32_t rc = timed_controller->configuration(controller->controller.context,
                              device->physical, out, length, vid, pid, left);
    rc = deadline_result(d, rc);
    if (rc != RISC_STREAM_OK) return rc > 0 ? RISC_STREAM_IO : rc;
    if (*length < 9 || *length > capacity || out[0] < 9 || out[1] != 2 ||
        ((size_t)out[2] | ((size_t)out[3] << 8)) != *length) return RISC_STREAM_IO;
    return deadline_left(d) ? RISC_STREAM_OK :
        (deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT);
}
static int32_t deadline_configuration(void *ctx, uint64_t token,
                                      uint8_t *out, size_t *length,
                                      uint16_t *vid, uint16_t *pid, uint32_t budget) {
    (void)ctx;
    if (!out || !length || !vid || !pid || *length < 9 ||
        *length > RISC_USB_CONFIG_LIMIT) return RISC_STREAM_INVALID;
    host_deadline d;
    int32_t rc = deadline_begin(&d, budget);
    if (rc == RISC_STREAM_OK) rc = deadline_inventory(&d);
    return rc == RISC_STREAM_OK ? deadline_config(&d, token, out, length, vid, pid) : rc;
}
static int32_t deadline_claim(void *ctx, uint64_t token, uint8_t iface,
                              uint8_t alt, uint64_t *out, uint32_t budget) {
    (void)ctx;
    if (!out) return RISC_STREAM_INVALID;
    *out = 0;
    host_deadline d;
    int32_t rc = deadline_begin(&d, budget);
    if (rc == RISC_STREAM_OK) rc = deadline_inventory(&d);
    if (rc != RISC_STREAM_OK) return rc;
    device_slot *device = device_for(token);
    if (!device) return RISC_STREAM_DISCONNECTED;
    size_t empty = RISC_USB_HOST_MAX_CLAIMS;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i) {
        if (claims[i].token && claims[i].device_token == token &&
            claims[i].interface_number == iface) return RISC_STREAM_BUSY;
        if (!claims[i].token && empty == RISC_USB_HOST_MAX_CLAIMS) empty = i;
    }
    if (empty == RISC_USB_HOST_MAX_CLAIMS || sequence == UINT64_MAX)
        return RISC_STREAM_LIMIT;
    size_t length = sizeof(descriptor);
    uint16_t vid = 0, pid = 0, in = 0, output = 0, intr = 0;
    rc = deadline_config(&d, token, descriptor, &length, &vid, &pid);
    if (rc != RISC_STREAM_OK) return rc;
    if (!parse_endpoints(length, iface, alt, &in, &output, &intr)) return RISC_STREAM_IO;
    uint32_t left = deadline_left(&d);
    if (!left) return deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT;
    /* Reserve the host identity before entering a side-effecting callback so
     * even a partial physical claim always has a stable retained owner. */
    uint64_t assigned = next_token(), physical = 0;
    rc = timed_controller->claim(controller->controller.context, device->physical,
                                  iface, alt, &physical, left);
    rc = deadline_result(&d, rc);
    bool duplicate = false;
    for (size_t i = 0; physical && i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (claims[i].token && claims[i].physical_claim == physical) duplicate = true;
    if (physical) {
        claims[empty] = (claim_slot){assigned, physical, token, iface, alt,
                                     in, output, intr, false};
        timed_claims[empty] = true;
        *out = assigned;
    }
    if (rc == RISC_STREAM_OK && physical && !duplicate && deadline_left(&d)) return RISC_STREAM_OK;
    if (physical || rc == RISC_STREAM_OK || rc == RISC_STREAM_RETAINED || rc > 0) {
        deadline_retained = true;
        if (physical) claims[empty].closing = true;
        return RISC_STREAM_RETAINED;
    }
    return rc;
}
static int32_t deadline_release(void *ctx, uint64_t token, uint32_t budget) {
    (void)ctx;
    host_deadline d;
    int32_t rc = deadline_begin(&d, budget);
    if (rc != RISC_STREAM_OK) return rc;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i) {
        claim_slot *c = &claims[i];
        if (!token || c->token != token) continue;
        uint32_t left = deadline_left(&d);
        if (!left) return deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT;
        c->closing = true;
        rc = timed_controller->release(controller->controller.context, c->physical_claim, left);
        rc = deadline_result(&d, rc);
        if (rc != RISC_STREAM_OK) {
            deadline_retained = true;
            return RISC_STREAM_RETAINED;
        }
        *c = (claim_slot){0};
        timed_claims[i] = false;
        return RISC_STREAM_OK;
    }
    return RISC_STREAM_INVALID;
}
static int32_t timed_control_claim(void *ctx, uint64_t token, uint8_t type,
                                   uint8_t request, uint16_t value, uint16_t index,
                                   uint8_t *payload, uint16_t length, uint32_t budget) {
    (void)ctx;
    if (length > RISC_USB_CONFIG_LIMIT || (length && !payload)) return RISC_STREAM_INVALID;
    host_deadline d;
    int32_t rc = deadline_begin(&d, budget);
    if (rc == RISC_STREAM_OK) rc = deadline_inventory(&d);
    if (rc != RISC_STREAM_OK) return rc;
    claim_slot *c = claim_for(token);
    if (!c || !device_for(c->device_token)) return RISC_STREAM_DISCONNECTED;
    uint8_t recipient = type & 0x1fu;
    if (recipient == 1u) {
        if (index > 255u || index != c->interface_number) return RISC_STREAM_DENIED;
    } else if (recipient == 0u) {
        for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
            if (claims[i].token && claims[i].token != token &&
                claims[i].device_token == c->device_token) return RISC_STREAM_DENIED;
    } else return RISC_STREAM_DENIED;
    uint32_t left = deadline_left(&d);
    if (!left) return deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT;
    device_slot *device = device_for(c->device_token);
    rc = timed_controller->control(controller->controller.context, device->physical,
                                    type, request, value, index, payload, length, left);
    rc = deadline_result(&d, rc);
    if (rc > length) { deadline_retained = true; return RISC_STREAM_RETAINED; }
    return rc;
}
static int32_t timed_bulk(void *ctx, uint64_t token, uint8_t endpoint,
                          uint8_t *dst, const uint8_t *src, size_t length,
                          uint32_t budget, bool reading) {
    (void)ctx;
    if (!length || length > RISC_USB_CONFIG_LIMIT || (endpoint & 0x70u) ||
        (reading ? (!dst || !(endpoint & 0x80u)) : (!src || (endpoint & 0x80u))))
        return RISC_STREAM_INVALID;
    host_deadline d;
    int32_t rc = deadline_begin(&d, budget);
    if (rc == RISC_STREAM_OK) rc = deadline_inventory(&d);
    if (rc != RISC_STREAM_OK) return rc;
    claim_slot *c = claim_for(token);
    if (!c || !device_for(c->device_token)) return RISC_STREAM_DISCONNECTED;
    if (!((reading ? c->bulk_in : c->bulk_out) & (uint16_t)(1u << (endpoint & 15u))))
        return RISC_STREAM_DENIED;
    uint32_t left = deadline_left(&d);
    if (!left) return deadline_retained ? RISC_STREAM_RETAINED : RISC_STREAM_TIMEOUT;
    rc = reading ? timed_controller->bulk_read(controller->controller.context,
                         c->physical_claim, endpoint, dst, length, left) :
                   timed_controller->bulk_write(controller->controller.context,
                         c->physical_claim, endpoint, src, length, left);
    rc = deadline_result(&d, rc);
    if (rc >= 0 && (size_t)rc > length) { deadline_retained = true; return RISC_STREAM_RETAINED; }
    return rc;
}
static const risc_usb_host_deadline_api_v1 deadline_api = {
    RISC_USB_HOST_DEADLINES_API_V1, sizeof(risc_usb_host_deadline_api_v1),
    deadline_now, deadline_snapshot, deadline_configuration, deadline_claim, deadline_release
};
