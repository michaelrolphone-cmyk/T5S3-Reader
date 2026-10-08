/* CDC-only optional Runtime session adapter. Legacy Reader callbacks remain
 * ABI-compatible; a tagged session cannot be adopted through those callbacks.
 * No retries, allocation, PHY/power operations or caller pointers retained. */
typedef struct { const risc_usb_host_deadline_api_v1 *api; uint64_t began; uint32_t ms; } cdc_deadline;

static const risc_usb_host_deadline_api_v1 *deadline_api(void) {
    if (!host || host->struct_size < sizeof(risc_usb_host_deadlines_v1)) return 0;
    const risc_usb_host_deadlines_v1 *ext = (const risc_usb_host_deadlines_v1 *)host;
    if (ext->extension_tag != RISC_USB_HOST_DEADLINES_TAG_V1 ||
        ext->extension_version != RISC_USB_HOST_DEADLINES_API_V1) return 0;
    const risc_usb_host_deadline_api_v1 *api = ext->deadlines;
    return api && api->api_version == RISC_USB_HOST_DEADLINES_API_V1 &&
        api->struct_size >= sizeof(*api) && api->now_ms && api->snapshot &&
        api->configuration && api->claim && api->release ? api : 0;
}
static bool begin_deadline(cdc_deadline *d, uint32_t ms) {
    d->api = deadline_api();
    if (!d->api || !ms || ms > 1000) return false;
    d->began = d->api->now_ms(host->context); d->ms = ms;
    return true;
}
static uint32_t remaining(const cdc_deadline *d) {
    const uint64_t now = d->api->now_ms(host->context);
    return now < d->began || now - d->began >= d->ms ? 0 : d->ms - (uint32_t)(now - d->began);
}
static bool overrun(const cdc_deadline *d) {
    const uint64_t now = d->api->now_ms(host->context);
    return now < d->began || now - d->began > d->ms;
}
static bool valid_config(const risc_serial_stream_config_v1 *c) {
    return c->baud_rate >= 300 && c->baud_rate <= 3000000 &&
        c->data_bits >= 5 && c->data_bits <= 8 && c->parity <= 4 &&
        (c->stop_bits == 1 || c->stop_bits == 2) && !c->flow_control;
}
static int32_t retain_tagged(cdc_session *s) {
    if (s->retained) return RISC_STREAM_RETAINED;
    tagged_fenced = true;
    s->failed = s->retained = true;
    /* Fence before the only permitted follow-up: bounded terminal notification.
     * No data, inventory, control, cleanup or retry follows a retained result.
     * RETAINED is visible even if nobody drains the queues. A failed finish
     * keeps IDs and custody; quiesce remains false independently. */
    if (s->rx) (void)streams->finish(streams->context, s->rx, RISC_STREAM_RETAINED);
    if (s->tx) (void)streams->finish(streams->context, s->tx, RISC_STREAM_RETAINED);
    return RISC_STREAM_RETAINED;
}
static void describe_tagged(const cdc_session *s, risc_provider_stream_session_v1 *out) {
    out->session = s->token; out->rx_endpoint = s->rx; out->tx_endpoint = s->tx;
}
static int32_t check_device(uint64_t device, const cdc_deadline *d) {
    uint32_t ms = remaining(d);
    if (!ms) return RISC_STREAM_TIMEOUT;
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    const int32_t rc = d->api->snapshot(host->context, tokens, &count, ms);
    if (overrun(d)) return RISC_STREAM_RETAINED;
    if (rc != RISC_STREAM_OK || count > RISC_USB_HOST_MAX_DEVICES)
        return rc == RISC_STREAM_RETAINED ? rc : RISC_STREAM_IO;
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        if (!tokens[i]) return RISC_STREAM_IO;
        for (size_t j = 0; j < i; ++j) if (tokens[i] == tokens[j]) return RISC_STREAM_IO;
        found |= tokens[i] == device;
    }
    return found ? RISC_STREAM_OK : RISC_STREAM_DISCONNECTED;
}
static int32_t tagged_configure(cdc_session *s, const risc_serial_stream_config_v1 *c,
                                const cdc_deadline *d) {
    const uint32_t ms = remaining(d);
    if (!ms) return RISC_STREAM_TIMEOUT;
    uint32_t baud = c->baud_rate;
    uint8_t payload[7] = {(uint8_t)baud, (uint8_t)(baud >> 8), (uint8_t)(baud >> 16),
        (uint8_t)(baud >> 24), c->stop_bits == 2 ? 2u : 0u, c->parity, c->data_bits};
    const int32_t n = host_discovery->control_claim(host->context, s->control_claim,
        0x21, 0x20, 0, s->control_interface, payload, 7, ms);
    if (n == RISC_STREAM_RETAINED || overrun(d)) return RISC_STREAM_RETAINED;
    return n == 7 ? RISC_STREAM_OK : RISC_STREAM_IO;
}
static int32_t cleanup_tagged(cdc_session *s, const cdc_deadline *d) {
    s->failed = true; /* revoke pumping before any close can fail */
    if (tagged_fenced || s->retained) return RISC_STREAM_RETAINED;
    /* Physical claims close BEFORE queues, including unpublished partial-open
     * queues. Runtime retains reserved queue slots until callback success. */
    if (s->data_claim && s->data_claim != s->control_claim) {
        const uint32_t ms = remaining(d);
        if (!ms || d->api->release(host->context, s->data_claim, ms) != RISC_STREAM_OK)
            return retain_tagged(s);
        s->data_claim = 0;
        if (overrun(d)) return retain_tagged(s);
    }
    if (s->control_claim) {
        const uint32_t ms = remaining(d);
        if (!ms || d->api->release(host->context, s->control_claim, ms) != RISC_STREAM_OK)
            return retain_tagged(s);
        s->control_claim = s->data_claim = 0;
        if (overrun(d)) return retain_tagged(s);
    }
    uint32_t *ids[2] = {&s->rx, &s->tx};
    for (size_t i = 0; i < 2; ++i) if (*ids[i]) {
        if (!remaining(d) || streams->close(streams->context, *ids[i]) != RISC_STREAM_OK)
            return retain_tagged(s);
        *ids[i] = 0;
        if (overrun(d)) return retain_tagged(s);
    }
    *s = (cdc_session){0};
    return RISC_STREAM_OK;
}
static int32_t open_tagged(const void *request, uint32_t size, uint32_t ms,
                           risc_provider_stream_session_v1 *out) {
    if (!out || out->struct_size < sizeof(*out)) return RISC_STREAM_INVALID;
    *out = (risc_provider_stream_session_v1){.struct_size = sizeof(*out)};
    if (!request || size != sizeof(risc_serial_stream_open_v1) || !ms || ms > 1000)
        return RISC_STREAM_INVALID;
    risc_serial_stream_open_v1 req;
    memcpy(&req, request, sizeof(req));
    if (req.api_version != RISC_SERIAL_STREAM_REQUEST_V1 || req.struct_size != sizeof(req) ||
        !req.provider_device || req.device_generation != req.provider_device || !valid_config(&req.config))
        return RISC_STREAM_INVALID;
    cdc_deadline d;
    if (tagged_fenced) return RISC_STREAM_RETAINED;
    if (!streams || !begin_deadline(&d, ms)) return RISC_STREAM_UNSUPPORTED;
    cdc_session *s = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        if (sessions[i].retained) return RISC_STREAM_RETAINED;
        if (sessions[i].token && sessions[i].device == req.provider_device) return RISC_STREAM_BUSY;
        if (!sessions[i].token && !s) s = &sessions[i];
    }
    if (!s || generation == UINT64_MAX) return RISC_STREAM_LIMIT;
    int32_t rc = check_device(req.provider_device, &d);
    if (rc != RISC_STREAM_OK) {
        if (rc == RISC_STREAM_RETAINED) tagged_fenced = true;
        return rc;
    }
    size_t length = sizeof(config_bytes);
    uint16_t vid = 0, pid = 0;
    uint32_t left = remaining(&d);
    if (!left) return RISC_STREAM_TIMEOUT;
    rc = d.api->configuration(host->context, req.provider_device, config_bytes, &length, &vid, &pid, left);
    if (overrun(&d) || rc == RISC_STREAM_RETAINED) { tagged_fenced = true; return RISC_STREAM_RETAINED; }
    if (rc != RISC_STREAM_OK) return RISC_STREAM_IO;
    cdc_session candidate = {0};
    if (!parse(length, &candidate)) return RISC_STREAM_UNSUPPORTED;
    candidate.device = req.provider_device; candidate.token = ++generation; candidate.tagged = true;
    *s = candidate;
    left = remaining(&d);
    if (!left) { rc = RISC_STREAM_TIMEOUT; goto rollback; }
    rc = d.api->claim(host->context, s->device, s->control_interface, 0, &s->control_claim, left);
    if (overrun(&d) || rc == RISC_STREAM_RETAINED) goto retained;
    if (rc != RISC_STREAM_OK && s->control_claim) goto retained;
    if (rc != RISC_STREAM_OK || !s->control_claim) { rc = RISC_STREAM_IO; goto rollback; }
    left = remaining(&d);
    if (!left) { rc = RISC_STREAM_TIMEOUT; goto rollback; }
    rc = d.api->claim(host->context, s->device, s->data_interface, s->data_alternate, &s->data_claim, left);
    if (overrun(&d) || rc == RISC_STREAM_RETAINED) goto retained;
    if ((rc != RISC_STREAM_OK && s->data_claim) || s->data_claim == s->control_claim) goto retained;
    if (rc != RISC_STREAM_OK || !s->data_claim) { rc = RISC_STREAM_IO; goto rollback; }
    rc = tagged_configure(s, &req.config, &d);
    if (rc == RISC_STREAM_RETAINED) goto retained;
    if (rc != RISC_STREAM_OK) goto rollback;
    const risc_stream_endpoint_v1 source = {sizeof(source), 1, RISC_STREAM_READ, 2048, 0, 0, 0};
    const risc_stream_endpoint_v1 sink = {sizeof(sink), 1, RISC_STREAM_WRITE, 2048, 0, 0, 0};
    if (!remaining(&d)) { rc = RISC_STREAM_TIMEOUT; goto rollback; }
    rc = streams->publish(streams->context, &source, &s->rx);
    if (overrun(&d) || rc == RISC_STREAM_RETAINED) goto retained;
    if (rc != RISC_STREAM_OK && s->rx) goto retained;
    if (rc != RISC_STREAM_OK || !s->rx) { rc = RISC_STREAM_IO; goto rollback; }
    if (!remaining(&d)) { rc = RISC_STREAM_TIMEOUT; goto rollback; }
    rc = streams->publish(streams->context, &sink, &s->tx);
    if (overrun(&d) || rc == RISC_STREAM_RETAINED) goto retained;
    if ((rc != RISC_STREAM_OK && s->tx) || s->tx == s->rx) goto retained;
    if (rc != RISC_STREAM_OK || !s->tx) { rc = RISC_STREAM_IO; goto rollback; }
    describe_tagged(s, out);
    return RISC_STREAM_OK;
rollback:
    if (cleanup_tagged(s, &d) == RISC_STREAM_OK) return rc;
retained:
    retain_tagged(s); describe_tagged(s, out);
    return RISC_STREAM_RETAINED;
}
static int32_t call_tagged(uint64_t token, const void *request, uint32_t size, uint32_t ms,
                           void *reply, uint32_t capacity, uint32_t *reply_size) {
    if (reply_size) *reply_size = 0;
    if (!reply_size || (!reply && capacity) || !request || size != sizeof(risc_serial_stream_call_v1) ||
        !ms || ms > 1000) return RISC_STREAM_INVALID;
    cdc_session *s = lookup(token);
    if (!s || !s->tagged) return RISC_STREAM_CLOSED;
    if (tagged_fenced || s->retained) return RISC_STREAM_RETAINED;
    risc_serial_stream_call_v1 req;
    memcpy(&req, request, sizeof(req));
    if (req.api_version != RISC_SERIAL_STREAM_REQUEST_V1 || req.struct_size != sizeof(req) || req.reserved)
        return RISC_STREAM_INVALID;
    if (req.operation == RISC_SERIAL_STREAM_CONFIGURE) {
        if (!valid_config(&req.value.config)) return RISC_STREAM_INVALID;
    } else if (req.operation == RISC_SERIAL_STREAM_CONTROL_LINES) {
        if (req.value.lines.dtr > 1 || req.value.lines.rts > 1) return RISC_STREAM_INVALID;
        for (size_t i = 0; i < 6; ++i) if (req.value.lines.reserved[i]) return RISC_STREAM_INVALID;
    } else if (req.operation == RISC_SERIAL_STREAM_CHECK_DEVICE) {
        for (size_t i = 0; i < sizeof(req.value.zero); ++i) if (req.value.zero[i]) return RISC_STREAM_INVALID;
    } else return RISC_STREAM_UNSUPPORTED;
    cdc_deadline d;
    if (!begin_deadline(&d, ms)) return retain_tagged(s);
    int32_t rc = check_device(s->device, &d);
    if (rc == RISC_STREAM_RETAINED) return retain_tagged(s);
    if (rc == RISC_STREAM_DISCONNECTED) fail_endpoints(s, rc);
    if (rc != RISC_STREAM_OK) return rc;
    if (req.operation == RISC_SERIAL_STREAM_CHECK_DEVICE) return RISC_STREAM_OK;
    if (s->failed) return RISC_STREAM_IO;
    if (req.operation == RISC_SERIAL_STREAM_CONFIGURE) rc = tagged_configure(s, &req.value.config, &d);
    else {
        const uint32_t left = remaining(&d);
        if (!left) return RISC_STREAM_TIMEOUT;
        const uint16_t value = (uint16_t)(req.value.lines.dtr | (req.value.lines.rts << 1));
        const int32_t n = host_discovery->control_claim(host->context, s->control_claim,
            0x21, 0x22, value, s->control_interface, 0, 0, left);
        rc = n == RISC_STREAM_RETAINED || overrun(&d) ? RISC_STREAM_RETAINED : n == 0 ? RISC_STREAM_OK : RISC_STREAM_IO;
    }
    return rc == RISC_STREAM_RETAINED ? retain_tagged(s) : rc;
}
static int32_t close_tagged(uint64_t token, uint32_t ms) {
    if (!ms || ms > 1000) return RISC_STREAM_INVALID;
    cdc_session *s = lookup(token);
    if (!s || !s->tagged) return RISC_STREAM_CLOSED;
    if (tagged_fenced || s->retained) return RISC_STREAM_RETAINED;
    cdc_deadline d;
    if (!begin_deadline(&d, ms)) return retain_tagged(s);
    return cleanup_tagged(s, &d);
}
static bool legacy_endpoints(uint64_t token, uint32_t *rx, uint32_t *tx) {
    cdc_session *s = lookup(token);
    if (s && s->tagged) { if (rx) *rx = 0; if (tx) *tx = 0; return false; }
    return endpoints(token, rx, tx);
}
static void tagged_pump(cdc_session *s, uint32_t ms) {
    if (tagged_fenced) return;
    cdc_deadline d;
    if (!begin_deadline(&d, ms > 1000 ? 1000 : ms)) { retain_tagged(s); return; }
    int32_t rc = check_device(s->device, &d);
    if (rc == RISC_STREAM_RETAINED) { retain_tagged(s); return; }
    if (rc == RISC_STREAM_DISCONNECTED) fail_endpoints(s, rc);
    if (rc != RISC_STREAM_OK) return; /* unknown inventory pauses all I/O */
    if (!remaining(&d)) return;
    bool flushed = flush_rx(s);
    if (overrun(&d)) { retain_tagged(s); return; }
    if (!flushed) return;
    if (!s->rx_size && remaining(&d)) {
        const int32_t n = read_data(s->token, staging[s - sessions].rx_pending, 256, 1);
        if (n == RISC_STREAM_RETAINED || overrun(&d)) { retain_tagged(s); return; }
        if (n < 0 || n > 256) { fail_endpoints(s, RISC_STREAM_IO); return; }
        s->rx_size = (uint32_t)n;
        if (!remaining(&d)) return;
        flushed = flush_rx(s);
        if (overrun(&d)) { retain_tagged(s); return; }
        if (!flushed) return;
    }
    if (!remaining(&d)) return;
    if (s->tx_size == s->tx_offset) {
        uint32_t n = 0;
        rc = streams->consume(streams->context, s->tx, staging[s - sessions].tx_pending, 256, &n);
        if (overrun(&d)) { retain_tagged(s); return; }
        if (rc < 0 || rc > 2 || n > 256) { fail_endpoints(s, RISC_STREAM_IO); return; }
        s->tx_size = n; s->tx_offset = 0;
    }
    if (remaining(&d) && s->tx_size > s->tx_offset) {
        uint32_t nbytes = s->tx_size - s->tx_offset;
        const int32_t n = write_data(s->token, staging[s - sessions].tx_pending + s->tx_offset, nbytes, 1);
        if (n == RISC_STREAM_RETAINED || overrun(&d)) { retain_tagged(s); return; }
        if (n < 0 || (uint32_t)n > nbytes) { fail_endpoints(s, RISC_STREAM_IO); return; }
        s->tx_offset += (uint32_t)n;
        if (s->tx_offset == s->tx_size) s->tx_offset = s->tx_size = 0;
    }
}
static void poll_cdc(uint32_t ms) { if (!tagged_fenced) poll_streams(ms); }
static const risc_stream_session_provider_v1 tagged_sessions = {
    RISC_STREAM_SESSION_PROVIDER_API_V1, sizeof(tagged_sessions), open_tagged, call_tagged, close_tagged
};
