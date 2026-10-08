/* Claim-scoped controls are required for tagged FTDI sessions. The raw Reader
 * path keeps its historical device controls and capability table unchanged. */
static bool tagged_parse(size_t length, uint16_t vid, uint16_t pid, serial_session *s) {
    return supported_pid(vid, pid) && parse_configuration(length, s) &&
           s->max_packet >= FTDI_RX_STATUS_BYTES;
}
static int32_t tagged_initialize(serial_session *s, uint16_t pid, const serial_deadline *d) {
    uint8_t bytes[18] = {0};
    int32_t rc = tagged_command(s, 0x80, USB_REQ_GET_DESCRIPTOR,
        (uint16_t)(USB_DESC_DEVICE << 8), 0, bytes, sizeof(bytes), d);
    if (rc != RISC_STREAM_OK) return rc;
    if (bytes[0] < sizeof(bytes) || bytes[1] != USB_DESC_DEVICE ||
        !classify(s, pid, (uint16_t)(bytes[12] | ((uint16_t)bytes[13] << 8))))
        return RISC_STREAM_UNSUPPORTED;
    rc = tagged_command(s, 0x40, FTDI_REQ_RESET, FTDI_RESET_SIO, s->channel, 0, 0, d);
    if (rc != RISC_STREAM_OK) return rc;
    rc = tagged_command(s, 0x40, FTDI_REQ_RESET, FTDI_PURGE_RX, s->channel, 0, 0, d);
    if (rc != RISC_STREAM_OK) return rc;
    rc = tagged_command(s, 0x40, FTDI_REQ_RESET, FTDI_PURGE_TX, s->channel, 0, 0, d);
    if (rc != RISC_STREAM_OK) return rc;
    return tagged_command(s, 0x40, FTDI_REQ_SET_FLOW_CTRL, 0, s->channel, 0, 0, d);
}
static int32_t tagged_configure(serial_session *s, const risc_serial_stream_config_v1 *c,
                                const serial_deadline *d) {
    uint16_t value = 0, index = 0;
    if (!baud_request(s, c->baud_rate, &value, &index)) return RISC_STREAM_UNSUPPORTED;
    uint16_t line = (uint16_t)(c->data_bits | (c->parity << 8) | (c->stop_bits == 2 ? 2u << 11 : 0));
    int32_t rc = tagged_command(s, 0x40, FTDI_REQ_SET_DATA, line, s->channel, 0, 0, d);
    if (rc != RISC_STREAM_OK) return rc;
    return tagged_command(s, 0x40, FTDI_REQ_SET_BAUD, value, index, 0, 0, d);
}
static int32_t tagged_control_lines(serial_session *s, bool dtr, bool rts, const serial_deadline *d) {
    uint16_t value = (uint16_t)(FTDI_DTR_MASK | FTDI_RTS_MASK |
        (dtr ? FTDI_DTR_HIGH : 0) | (rts ? FTDI_RTS_HIGH : 0));
    return tagged_command(s, 0x40, FTDI_REQ_MODEM_CTRL, value, s->channel, 0, 0, d);
}
static int32_t tagged_before_release(serial_session *s, const serial_deadline *d) {
    /* An unclassified device has had no UART controls. A known detach cannot
     * acknowledge line teardown; unknown inventory is never proof of detach. */
    if (!s->chip) return RISC_STREAM_OK;
    int32_t rc = check_device(s->device, d);
    return rc == RISC_STREAM_DISCONNECTED ? RISC_STREAM_OK :
           rc == RISC_STREAM_OK ? tagged_control_lines(s, false, false, d) : rc;
}
