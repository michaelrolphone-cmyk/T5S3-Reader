static void uc_build_lut(uint8_t out[5][UC_LUT_BYTES], uint8_t frames, bool absolute) {
    for (size_t t = 0; t < 5u; ++t)
        for (size_t i = 0; i < UC_LUT_BYTES; ++i) out[t][i] = 0;
    for (size_t t = 0; t < 5u; ++t) {
        out[t][0] = 0x01;
        out[t][5] = 0x01;
        out[t][6] = 0x01;
    }
    out[0][1] = frames;
    if (absolute) {
        out[1][1] = (uint8_t)(0x40u | frames); /* target white */
        out[2][1] = (uint8_t)(0x80u | frames); /* target black */
        out[3][1] = (uint8_t)(0x40u | frames);
        out[4][1] = (uint8_t)(0x80u | frames);
    } else {
        out[1][1] = frames;                    /* WW idle */
        out[2][1] = (uint8_t)(0x80u | frames); /* W->B */
        out[3][1] = (uint8_t)(0x40u | frames); /* B->W */
        out[4][1] = frames;                    /* BB idle */
    }
}
static bool uc_upload_lut(uint8_t frames, bool absolute) {
    uint8_t lut[5][UC_LUT_BYTES];
    uc_build_lut(lut, frames, absolute);
    for (uint8_t t = 0; t < 5u; ++t)
        if (!uc_register((uint8_t)(0x20u + t), lut[t], UC_LUT_BYTES)) return false;
    return true;
}
static bool uc_power_on(uint64_t deadline_ms) {
    if (uc_powered) return true;
    if (!uc_command(0x04)) return false;
    const uint64_t start = now_ms();
    if (start == UINT64_MAX) return false;
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        const uint64_t now = now_ms();
        if (now == UINT64_MAX || now >= deadline_ms || now - start >= UC_BUSY_ASSERT_MS) {
            set_reason("power busy never asserted");
            return false;
        }
        sleep_ms(1);
    }
    if (!uc_wait_idle_high(deadline_ms, "uc power-on timeout")) return false;
    uc_powered = true;
    return true;
}
static bool uc_external_refresh(const risc_display_rect_v1 *r, uint8_t frames,
                                bool absolute, uint64_t deadline_ms) {
    const uint8_t psr[] = {0x37, 0x4d};
    if (!uc_register1(0x30, 0x0f) || !uc_set_window(r) ||
        !uc_register(0x00, psr, sizeof(psr)) || !uc_register1(0x03, 0x20) ||
        !uc_register1(0xe1, 0x02) || !uc_register1(0x50, 0xd7) ||
        !uc_register1(0xe0, 0x02) || !uc_register1(0xe5, 0x5a) ||
        !uc_upload_lut(frames, absolute) || !uc_power_on(deadline_ms)) return false;
    refresh_ms = now_ms();
    if (refresh_ms == UINT64_MAX || !uc_wait_refresh(deadline_ms, UC_MIN_BUSY_MS)) return false;
    return uc_end_window();
}
