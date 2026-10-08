/* Shared tagged admission and custody for single-claim vendor USB serial
 * classes. Vendor hooks own their actual controls and descriptor policy.
 * Runtime serializes calls; request bytes and queue payloads are copied. */
static int32_t cleanup_tagged(serial_session *s, const serial_deadline *d) {
    s->failed = true; /* revoke pumping before any close can fail */
    if (tagged_fenced || s->retained) return RISC_STREAM_RETAINED;
    /* Physical claims close BEFORE queues, including unpublished partial-open
     * queues. Runtime retains reserved queue slots until callback success. */
    if (s->claim) {
        if (tagged_before_release(s, d) != RISC_STREAM_OK) return retain_tagged(s);
        const uint32_t ms = remaining(d);
        if (!ms || d->api->release(host->context, s->claim, ms) != RISC_STREAM_OK)
            return retain_tagged(s);
        s->claim = 0;
        if (overrun(d)) return retain_tagged(s);
    }
    uint32_t *ids[2] = {&s->rx, &s->tx};
    for (size_t i = 0; i < 2; ++i) if (*ids[i]) {
        if (!remaining(d) || streams->close(streams->context, *ids[i]) != RISC_STREAM_OK)
            return retain_tagged(s);
        *ids[i] = 0;
        if (overrun(d)) return retain_tagged(s);
    }
    *s = (serial_session){0};
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
    serial_deadline d;
    if (tagged_fenced) return RISC_STREAM_RETAINED;
    if (!streams || !begin_deadline(&d, ms)) return RISC_STREAM_UNSUPPORTED;
    serial_session *s = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        if (sessions[i].retained) return RISC_STREAM_RETAINED;
        if (sessions[i].token && sessions[i].device == req.provider_device) return RISC_STREAM_BUSY;
        if (!sessions[i].token && !sessions[i].claim && !s) s = &sessions[i];
    }
    if (!s || TAGGED_GENERATION == UINT64_MAX) return RISC_STREAM_LIMIT;
    int32_t rc = check_device(req.provider_device, &d);
    if (rc != RISC_STREAM_OK) {
        if (rc == RISC_STREAM_RETAINED) tagged_fenced = true;
        return rc;
    }
    size_t length = sizeof(TAGGED_CONFIG_BYTES);
    uint16_t vid = 0, pid = 0;
    uint32_t left = remaining(&d);
    if (!left) return RISC_STREAM_TIMEOUT;
    rc = d.api->configuration(host->context, req.provider_device, TAGGED_CONFIG_BYTES, &length, &vid, &pid, left);
    if (rc == RISC_STREAM_RETAINED || overrun(&d)) { tagged_fenced = true; return RISC_STREAM_RETAINED; }
    if (rc != RISC_STREAM_OK) return RISC_STREAM_IO;
    serial_session candidate = {0};
    if (!tagged_parse(length, vid, pid, &candidate)) return RISC_STREAM_UNSUPPORTED;
    candidate.device = req.provider_device; candidate.token = ++TAGGED_GENERATION; candidate.tagged = true;
    *s = candidate;
    left = remaining(&d);
    if (!left) { rc = RISC_STREAM_TIMEOUT; goto rollback; }
    rc = d.api->claim(host->context, s->device, TAGGED_IFACE(s), TAGGED_ALT(s), &s->claim, left);
    if (rc == RISC_STREAM_RETAINED || overrun(&d)) goto retained;
    if (rc != RISC_STREAM_OK && s->claim) goto retained;
    if (rc != RISC_STREAM_OK || !s->claim) { rc = RISC_STREAM_IO; goto rollback; }
    rc = tagged_initialize(s, pid, &d);
    if (rc == RISC_STREAM_RETAINED) goto retained;
    if (rc != RISC_STREAM_OK) goto rollback;
    rc = tagged_configure(s, &req.config, &d);
    if (rc == RISC_STREAM_RETAINED) goto retained;
    if (rc != RISC_STREAM_OK) goto rollback;
    const risc_stream_endpoint_v1 source = {sizeof(source), 1, RISC_STREAM_READ, 2048, 0, 0, 0};
    const risc_stream_endpoint_v1 sink = {sizeof(sink), 1, RISC_STREAM_WRITE, 2048, 0, 0, 0};
    if (!remaining(&d)) { rc = RISC_STREAM_TIMEOUT; goto rollback; }
    rc = streams->publish(streams->context, &source, &s->rx);
    if (rc == RISC_STREAM_RETAINED || overrun(&d)) goto retained;
    if (rc != RISC_STREAM_OK && s->rx) goto retained;
    if (rc != RISC_STREAM_OK || !s->rx) { rc = RISC_STREAM_IO; goto rollback; }
    if (!remaining(&d)) { rc = RISC_STREAM_TIMEOUT; goto rollback; }
    rc = streams->publish(streams->context, &sink, &s->tx);
    if (rc == RISC_STREAM_RETAINED || overrun(&d)) goto retained;
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
    serial_session *s = lookup(token);
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
    serial_deadline d;
    if (!begin_deadline(&d, ms)) return retain_tagged(s);
    int32_t rc = check_device(s->device, &d);
    if (rc == RISC_STREAM_RETAINED) return retain_tagged(s);
    if (rc == RISC_STREAM_DISCONNECTED) fail_endpoints(s, rc);
    if (rc != RISC_STREAM_OK) return rc;
    if (req.operation == RISC_SERIAL_STREAM_CHECK_DEVICE) return RISC_STREAM_OK;
    if (s->failed) return RISC_STREAM_IO;
    if (req.operation == RISC_SERIAL_STREAM_CONFIGURE) rc = tagged_configure(s, &req.value.config, &d);
    else rc = tagged_control_lines(s, req.value.lines.dtr, req.value.lines.rts, &d);
    return rc == RISC_STREAM_RETAINED ? retain_tagged(s) : rc;
}
static int32_t close_tagged(uint64_t token, uint32_t ms) {
    if (!ms || ms > 1000) return RISC_STREAM_INVALID;
    serial_session *s = lookup(token);
    if (!s || !s->tagged) return RISC_STREAM_CLOSED;
    if (tagged_fenced || s->retained) return RISC_STREAM_RETAINED;
    serial_deadline d;
    if (!begin_deadline(&d, ms)) return retain_tagged(s);
    return cleanup_tagged(s, &d);
}
#ifdef TAGGED_LEGACY_ENDPOINTS
static bool legacy_endpoints(uint64_t token, uint32_t *rx, uint32_t *tx) {
    serial_session *s = lookup(token);
    if (s && s->tagged) { if (rx) *rx = 0; if (tx) *tx = 0; return false; }
    return endpoints(token, rx, tx);
}
#endif
/* A retained queue callback ends this turn immediately, including clock and
 * terminal-notification callbacks. Legacy flush behavior remains unchanged. */
static bool tagged_flush_rx(serial_session *s, const serial_deadline *d) {
    if (s->rx_size == s->rx_offset) return true;
    const uint32_t count = s->rx_size - s->rx_offset;
    uint32_t accepted = 0;
    const int32_t rc = streams->produce(streams->context, s->rx,
        staging[s - sessions].rx_pending + s->rx_offset, count, &accepted);
    if (rc == RISC_STREAM_RETAINED || overrun(d)) { retain_tagged(s); return false; }
    if (rc < 0 || rc > 1 || accepted > count) { fail_endpoints(s, RISC_STREAM_IO); return false; }
    s->rx_offset += accepted;
    if (s->rx_offset == s->rx_size) s->rx_offset = s->rx_size = 0;
    return true;
}
static void tagged_pump(serial_session *s, uint32_t ms) {
    if (tagged_fenced) return;
    serial_deadline d;
    if (!begin_deadline(&d, ms > 1000 ? 1000 : ms)) { retain_tagged(s); return; }
    int32_t rc = check_device(s->device, &d);
    if (rc == RISC_STREAM_RETAINED) { retain_tagged(s); return; }
    if (rc == RISC_STREAM_DISCONNECTED) fail_endpoints(s, rc);
    if (rc != RISC_STREAM_OK) return; /* unknown inventory pauses all I/O */
    if (!remaining(&d)) return;
    if (!tagged_flush_rx(s, &d)) return;
    if (!s->rx_size && remaining(&d)) {
        const serial_deadline io = {d.api, d.api->now_ms(host->context), 1};
        const int32_t n = read_data(s->token, staging[s - sessions].rx_pending, 256, 1);
        if (n == RISC_STREAM_RETAINED || overrun(&io) || overrun(&d)) { retain_tagged(s); return; }
        if (n < 0 || n > 256) { fail_endpoints(s, RISC_STREAM_IO); return; }
        s->rx_size = (uint32_t)n;
        if (!remaining(&d)) return;
        if (!tagged_flush_rx(s, &d)) return;
    }
    if (!remaining(&d)) return;
    if (s->tx_size == s->tx_offset) {
        uint32_t n = 0;
        rc = streams->consume(streams->context, s->tx, staging[s - sessions].tx_pending, 256, &n);
        if (rc == RISC_STREAM_RETAINED || overrun(&d)) { retain_tagged(s); return; }
        if (rc < 0 || rc > 2 || n > 256) { fail_endpoints(s, RISC_STREAM_IO); return; }
        s->tx_size = n; s->tx_offset = 0;
    }
    if (remaining(&d) && s->tx_size > s->tx_offset) {
        uint32_t nbytes = s->tx_size - s->tx_offset;
        const serial_deadline io = {d.api, d.api->now_ms(host->context), 1};
        const int32_t n = write_data(s->token, staging[s - sessions].tx_pending + s->tx_offset, nbytes, 1);
        if (n == RISC_STREAM_RETAINED || overrun(&io) || overrun(&d)) { retain_tagged(s); return; }
        if (n < 0 || (uint32_t)n > nbytes) { fail_endpoints(s, RISC_STREAM_IO); return; }
        s->tx_offset += (uint32_t)n;
        if (s->tx_offset == s->tx_size) s->tx_offset = s->tx_size = 0;
    }
}
static void poll_tagged_serial(uint32_t ms) { if (!tagged_fenced) poll_streams(ms); }
static const risc_stream_session_provider_v1 tagged_sessions = {
    RISC_STREAM_SESSION_PROVIDER_API_V1, sizeof(tagged_sessions), open_tagged, call_tagged, close_tagged
};
