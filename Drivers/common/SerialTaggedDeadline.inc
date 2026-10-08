/* Private optional USB serial session mechanics; no ABI or firmware imports. */
typedef struct { const risc_usb_host_deadline_api_v1 *api; uint64_t began; uint32_t ms; } serial_deadline;

static const risc_usb_host_deadline_api_v1 *deadline_api(void) {
    if (!host || host->struct_size < sizeof(risc_usb_host_deadlines_v1)) return 0;
    const risc_usb_host_deadlines_v1 *ext = (const risc_usb_host_deadlines_v1 *)host;
    if (ext->extension_tag != RISC_USB_HOST_DEADLINES_TAG_V1 ||
        ext->extension_version != RISC_USB_HOST_DEADLINES_API_V1 ||
        !ext->snapshot.discovery.control_claim) return 0;
    const risc_usb_host_deadline_api_v1 *api = ext->deadlines;
    return api && api->api_version == RISC_USB_HOST_DEADLINES_API_V1 &&
        api->struct_size >= sizeof(*api) && api->now_ms && api->snapshot &&
        api->configuration && api->claim && api->release ? api : 0;
}
static bool begin_deadline(serial_deadline *d, uint32_t ms) {
    d->api = deadline_api();
    if (!d->api || !ms || ms > 1000) return false;
    d->began = d->api->now_ms(host->context); d->ms = ms;
    return true;
}
static uint32_t remaining(const serial_deadline *d) {
    const uint64_t now = d->api->now_ms(host->context);
    return now < d->began || now - d->began >= d->ms ? 0 : d->ms - (uint32_t)(now - d->began);
}
static bool overrun(const serial_deadline *d) {
    const uint64_t now = d->api->now_ms(host->context);
    return now < d->began || now - d->began > d->ms;
}
static bool valid_config(const risc_serial_stream_config_v1 *c) {
    return c->baud_rate >= 300 && c->baud_rate <= TAGGED_MAX_BAUD &&
        c->data_bits >= 5 && c->data_bits <= 8 && c->parity <= 4 &&
        (c->stop_bits == 1 || c->stop_bits == 2) && !c->flow_control;
}
static int32_t retain_tagged(serial_session *s) {
    const bool notify = !s->retained;
    tagged_fenced = true;
    s->failed = s->retained = true;
    /* Fence before notifying Runtime. These two bounded terminal notices are
     * the only callbacks allowed after retention; no data, cleanup or retry.
     * Refused notification never clears custody or permits another attempt. */
    if (notify && s->rx) (void)streams->finish(streams->context, s->rx, RISC_STREAM_RETAINED);
    if (notify && s->tx) (void)streams->finish(streams->context, s->tx, RISC_STREAM_RETAINED);
    return RISC_STREAM_RETAINED;
}
static void describe_tagged(const serial_session *s, risc_provider_stream_session_v1 *out) {
    out->session = s->token; out->rx_endpoint = s->rx; out->tx_endpoint = s->tx;
}
static int32_t check_device(uint64_t device, const serial_deadline *d) {
    uint32_t ms = remaining(d);
    if (!ms) return RISC_STREAM_TIMEOUT;
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    const int32_t rc = d->api->snapshot(host->context, tokens, &count, ms);
    if (rc == RISC_STREAM_RETAINED || overrun(d)) return RISC_STREAM_RETAINED;
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
/* Every command gets the remaining TOTAL caller budget. Short IN/OUT
 * responses fail, including nonzero results for zero-length controls. */
static int32_t tagged_command(serial_session *s, uint8_t type, uint8_t request,
                               uint16_t value, uint16_t index, uint8_t *data,
                               uint16_t length, const serial_deadline *d) {
    const uint32_t ms = remaining(d);
    if (!ms) return RISC_STREAM_TIMEOUT;
    const risc_usb_host_discovery_v1 *api = (const risc_usb_host_discovery_v1 *)host;
    const int32_t n = api->control_claim(host->context, s->claim, type, request,
                                        value, index, data, length, ms);
    if (n == RISC_STREAM_RETAINED || overrun(d)) return RISC_STREAM_RETAINED;
    return n == length ? RISC_STREAM_OK : RISC_STREAM_IO;
}
