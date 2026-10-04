#pragma once
/* Reused PCF8563/BM8563 register operations from RiscRTE-T-Watch-S3
 * drivers/twatch_rtc/driver.c at 9cfa2aa4d572a290b41cf040a27cbec9d78bb35c.
 * Host wrapper owns bus/claim/start/error state. Changes: commit valid output
 * atomically, reject STOP/test mode, diagnostics, explicit-write-only restart,
 * and no alarm enable without verified IRQ routing. No Watch bytes changed.
 */
static uint8_t bcd(uint8_t v) {
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}
static uint8_t dec(uint8_t v) {
    return (uint8_t)((v >> 4) * 10u + (v & 0x0fu));
}
static bool write_regs(uint8_t reg, const uint8_t *bytes, size_t n) {
    uint8_t buf[8];
    if (n > 7u)
        return false;
    buf[0] = reg;
    for (size_t i = 0; i < n; ++i)
        buf[i + 1u] = bytes[i];
    return bus->transact(bus->context, claim, buf, n + 1u, NULL, 0, 30);
}
static bool read_regs(uint8_t reg, uint8_t *out, size_t n) {
    return bus->transact(bus->context, claim, &reg, 1, out, n, 30);
}
static bool read_time(void *context, risc_rtc_time_v2 *out) {
    (void)context;
    if (!started || !out)
        return false;
    uint8_t raw[7] = {0};
    uint8_t control;
    if (!read_regs(0, &control, 1) || !read_regs(0x02u, raw, 7)) {
        fail("rtc read I/O"); return false;
    }
    if (control & 0xa8u) { fail("rtc stopped/test mode"); return false; }
    if (raw[0] & 0x80u) { fail("rtc voltage-low: time invalid"); return false; }
    if (raw[5] & 0x80u) { fail("rtc century outside 2000-2099"); return false; }
    for (size_t i = 0; i < 7; i++)
        if (i != 4 && !risc_rtc_valid_bcd(raw[i] & (i == 0 || i == 1   ? 0x7f
                                              : i == 2 || i == 3 ? 0x3f
                                              : i == 5           ? 0x1f
                                                                 : 0xff)))
            { fail("rtc invalid BCD"); return false; }
    risc_rtc_time_v2 value = {0};
    value.second = dec(raw[0] & 0x7fu);
    value.minute = dec(raw[1] & 0x7fu);
    value.hour = dec(raw[2] & 0x3fu);
    value.day = dec(raw[3] & 0x3fu);
    value.weekday = raw[4] & 0x07u;
    value.month = dec(raw[5] & 0x1fu);
    value.year = (uint16_t)(2000u + dec(raw[6]));
    if (!risc_rtc_valid_time_v2(&value)) { fail("rtc invalid calendar"); return false; }
    *out = value;
    last_error_text[0] = 0;
    return true;
}
static bool write_time(void *context, const risc_rtc_time_v2 *in) {
    (void)context;
    if (!started || !risc_rtc_valid_time_v2(in))
        return false;
    uint8_t raw[7] = {bcd(in->second),
                      bcd(in->minute),
                      bcd(in->hour),
                      bcd(in->day),
                      in->weekday & 0x07u,
                      bcd(in->month),
                      bcd((uint8_t)(in->year - 2000u))};
    uint8_t control;
    if (!read_regs(0, &control, 1) || (control & 0x88u)) {
        fail("rtc control read/test mode"); return false;
    }
    if (!write_regs(0x02u, raw, 7)) { fail("rtc time write I/O"); return false; }
    /* Only an explicit valid-time write may restart a stopped clock. */
    if (control & 0x20u) {
        // PCF8563 control N bits can read either value but must be written 0.
        // TEST1/TESTC are rejected above; normal running control is all-zero.
        control = 0;
        if (!write_regs(0, &control, 1)) { fail("rtc restart I/O"); return false; }
    }
    last_error_text[0] = 0;
    return true;
}
/* 255 disables that alarm compare field. PCF8563 minute-resolution alarm. */
static bool alarm(void *c, uint8_t minute, uint8_t hour, uint8_t day, uint8_t weekday,
                  bool enable) {
    (void)c;
    /* No verified RTC IRQ routing on X4. Never drive an unowned interrupt. */
    if (enable) { fail("rtc alarm IRQ unavailable"); return false; }
    if (!started || (minute != 255 && minute > 59) || (hour != 255 && hour > 23) ||
        (day != 255 && (!day || day > 31)) || (weekday != 255 && weekday > 6))
        return false;
    uint8_t fields[] = {minute == 255 ? 0x80 : bcd(minute), hour == 255 ? 0x80 : bcd(hour),
                        day == 255 ? 0x80 : bcd(day), weekday == 255 ? 0x80 : weekday},
            status;
    if (!read_regs(1, &status, 1))
        return false;
    uint8_t off = status & 0x15u; // Clear AF/AIE and reserved N bits 7:5.
    if (!write_regs(1, &off, 1) || !write_regs(9, fields, 4))
        return false;
    off |= enable ? 2 : 0;
    return write_regs(1, &off, 1);
}
static bool alarm_pending(void *c, bool *pending, bool ack) {
    (void)c;
    if (!started || !pending)
        return false;
    uint8_t v;
    if (!read_regs(1, &v, 1))
        return false;
    *pending = (v & 8) != 0;
    if (ack) {
        v &= 0x17u; // Clear AF and reserved N bits 7:5; keep defined peers.
        return write_regs(1, &v, 1);
    }
    return true;
}
