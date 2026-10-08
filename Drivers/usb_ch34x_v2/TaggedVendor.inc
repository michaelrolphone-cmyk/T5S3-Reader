/* CH34x protocol stays in the class ELF; use the existing divisor rules. */
static bool tagged_parse(size_t length, uint16_t vid, uint16_t pid, serial_session *s) {
    return supported(vid, pid) && parse(length, s);
}
static int32_t tagged_initialize(serial_session *s, uint16_t pid, const serial_deadline *d) {
    (void)pid;
    uint8_t version[2] = {0};
    int32_t rc = tagged_command(s, 0xc0, 0x5f, 0, 0, version, 2, d);
    if (rc != RISC_STREAM_OK) return rc;
    s->version = version[0];
    return tagged_command(s, 0x40, 0xa1, 0, 0, 0, 0, d);
}
static int32_t tagged_configure(serial_session *s, const risc_serial_stream_config_v1 *c,
                                const serial_deadline *d) {
    if (s->version < 0x30 && (c->data_bits != 8 || c->parity || c->stop_bits != 1))
        return RISC_STREAM_UNSUPPORTED;
    uint16_t value = 0;
    if (!divisor(c->baud_rate, s->version, &value)) return RISC_STREAM_UNSUPPORTED;
    int32_t rc = tagged_command(s, 0x40, 0x9a, 0x1312, value, 0, 0, d);
    if (rc != RISC_STREAM_OK || s->version < 0x30) return rc;
    uint8_t lcr = (uint8_t)(0xc0 | (c->data_bits - 5));
    if (c->parity) lcr |= (uint8_t)(0x08 | ((c->parity - 1) << 4));
    if (c->stop_bits == 2) lcr |= 0x04;
    return tagged_command(s, 0x40, 0x9a, 0x2518, lcr, 0, 0, d);
}
static int32_t tagged_control_lines(serial_session *s, bool dtr, bool rts, const serial_deadline *d) {
    uint16_t lines = (uint16_t)((dtr ? 0x20 : 0) | (rts ? 0x40 : 0));
    return tagged_command(s, 0x40, 0xa4, (uint16_t)~lines, 0, 0, 0, d);
}
static int32_t tagged_before_release(serial_session *s, const serial_deadline *d) {
    (void)s; (void)d;
    return RISC_STREAM_OK;
}
