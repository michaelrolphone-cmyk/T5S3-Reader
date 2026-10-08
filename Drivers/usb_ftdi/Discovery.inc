/* The capability prefix has no timeout parameter, so this read-only discovery
 * turn has one fixed total budget. Never fall back to legacy unbounded host
 * callbacks. Device classification controls stay in tagged/legacy open. */
#define FTDI_DISCOVERY_BUDGET_MS 250u
static void fence_discovery(void) {
    tagged_fenced = true;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].tagged) (void)retain_tagged(&sessions[i]);
}
static bool discovery_result(int32_t rc, const serial_deadline *d) {
    if (rc == RISC_STREAM_RETAINED || overrun(d)) { fence_discovery(); return false; }
    return rc == RISC_STREAM_OK && remaining(d);
}
static bool discovery_tokens(uint64_t *tokens, size_t *count, const serial_deadline *d) {
    const uint32_t ms = remaining(d);
    if (!ms) return false;
    *count = RISC_USB_HOST_MAX_DEVICES;
    const int32_t rc = d->api->snapshot(host->context, tokens, count, ms);
    if (!discovery_result(rc, d) || *count > RISC_USB_HOST_MAX_DEVICES) return false;
    for (size_t i = 0; i < *count; ++i) {
        if (!tokens[i]) return false;
        for (size_t j = 0; j < i; ++j) if (tokens[i] == tokens[j]) return false;
    }
    return true;
}
static int32_t probe_timed(uint64_t device, const serial_deadline *d) {
    const uint32_t ms = remaining(d);
    if (!ms) return -1;
    size_t length = sizeof(descriptors);
    uint16_t vid = 0, pid = 0;
    const int32_t rc = d->api->configuration(host->context, device, descriptors,
                                            &length, &vid, &pid, ms);
    if (!discovery_result(rc, d)) return -1;
    /* A supported identity with unreadable/malformed descriptors is unknown,
     * not evidence of absence. Ambiguous supported shapes stay unknown. */
    if (length < 9 || length > sizeof(descriptors) || descriptors[0] < 9 || descriptors[1] != 2 ||
        ((size_t)descriptors[2] | ((size_t)descriptors[3] << 8)) != length) return -1;
    if (!supported_pid(vid, pid)) return 0;
    ftdi_session candidate = {0};
    return parse_configuration(length, &candidate) && candidate.max_packet >= FTDI_RX_STATUS_BYTES ? 1 : -1;
}
static int32_t probe_device(uint64_t device) {
    serial_deadline d;
    if (!device || tagged_fenced || !begin_deadline(&d, FTDI_DISCOVERY_BUDGET_MS)) return -1;
    int32_t rc = check_device(device, &d);
    if (rc == RISC_STREAM_RETAINED) { fence_discovery(); return -1; }
    if (rc == RISC_STREAM_DISCONNECTED) return 0;
    return rc == RISC_STREAM_OK ? probe_timed(device, &d) : -1;
}
static bool snapshot_devices(risc_serial_device_v1 *out, size_t *inout_count) {
    serial_deadline d;
    if (!inout_count || tagged_fenced || !begin_deadline(&d, FTDI_DISCOVERY_BUDGET_MS)) return false;
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = 0;
    if (!discovery_tokens(tokens, &count, &d)) return false;
    risc_serial_device_v1 found[RISC_SERIAL_INVENTORY_MAX_DEVICES] = {{0}};
    size_t matches = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t decision = probe_timed(tokens[i], &d);
        if (decision < 0) return false;
        if (decision) found[matches++] = (risc_serial_device_v1){
            .provider_device = tokens[i], .generation = tokens[i], .transport = RISC_SERIAL_TRANSPORT_USB};
    }
    /* Each descriptor call may process new host events. Publish only when a
     * second complete snapshot proves the same set, independent of ordering. */
    uint64_t current[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t current_count = 0;
    if (!discovery_tokens(current, &current_count, &d) || current_count != count) return false;
    for (size_t i = 0; i < count; ++i) {
        bool present = false;
        for (size_t j = 0; j < count; ++j) present |= tokens[i] == current[j];
        if (!present) return false;
    }
    if (*inout_count < matches || (matches && !out)) { *inout_count = matches; return false; }
    reconcile_endpoints(tokens, count); /* Legacy queue sessions only; tagged tokens are skipped. */
    if (!discovery_result(RISC_STREAM_OK, &d)) return false;
    for (size_t i = 0; i < matches; ++i) out[i] = found[i];
    *inout_count = matches;
    return true;
}
