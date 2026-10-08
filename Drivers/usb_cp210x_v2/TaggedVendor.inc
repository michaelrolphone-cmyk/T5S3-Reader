/* CP210x UART enable, framing and disable remain separate acknowledged stages. */
static bool tagged_parse(size_t length, uint16_t vid, uint16_t pid, serial_session *s) {
    (void)pid;
    if (vid != 0x10c4 || length < 9 || length > sizeof(descriptors) ||
        ((size_t)descriptors[2] | ((size_t)descriptors[3] << 8)) != length || !parse(length, s))
        return false;
    return (s->in_ep & 0x0f) && !(s->in_ep & 0x70) &&
           (s->out_ep & 0x0f) && !(s->out_ep & 0x70);
}
static int32_t tagged_initialize(serial_session *s, uint16_t pid, const serial_deadline *d) {
    (void)pid;
    return tagged_command(s, 0x41, 0, 1, s->interface_number, 0, 0, d);
}
static int32_t tagged_configure(serial_session *s, const risc_serial_stream_config_v1 *c,
                                const serial_deadline *d) {
    uint32_t baud = c->baud_rate;
    uint8_t speed[4] = {(uint8_t)baud, (uint8_t)(baud >> 8),
                       (uint8_t)(baud >> 16), (uint8_t)(baud >> 24)};
    int32_t rc = tagged_command(s, 0x41, 0x1e, 0, s->interface_number, speed, 4, d);
    if (rc != RISC_STREAM_OK) return rc;
    uint16_t line = (uint16_t)((c->data_bits << 8) | (c->parity << 4) |
                              (c->stop_bits == 2 ? 2 : 0));
    return tagged_command(s, 0x41, 3, line, s->interface_number, 0, 0, d);
}
static int32_t tagged_control_lines(serial_session *s, bool dtr, bool rts, const serial_deadline *d) {
    uint16_t lines = (uint16_t)(0x0300 | (dtr ? 1 : 0) | (rts ? 2 : 0));
    return tagged_command(s, 0x41, 7, lines, s->interface_number, 0, 0, d);
}
static int32_t tagged_before_release(serial_session *s, const serial_deadline *d) {
    if (s->disabled) return RISC_STREAM_OK;
    int32_t rc = check_device(s->device, d);
    if (rc == RISC_STREAM_OK) rc = tagged_command(s, 0x41, 0, 0, s->interface_number, 0, 0, d);
    else if (rc == RISC_STREAM_DISCONNECTED) rc = RISC_STREAM_OK;
    if (rc == RISC_STREAM_OK) s->disabled = true;
    return rc;
}
