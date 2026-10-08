/* ---------------- SSD1677 established backend ---------------- */
static const uint8_t ssd_x_window[] = {0x00, 0x00, 0x1f, 0x03};
static const uint8_t ssd_y_window[] = {0xdf, 0x01, 0x00, 0x00};
static bool ssd_ready_for(const char *failure) {
    if (!x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) return true;
    const uint64_t start = now_ms();
    if (start == UINT64_MAX) return false;
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        const uint64_t now = now_ms();
        if (now == UINT64_MAX || now - start >= 500u) { set_reason(failure); return false; }
        sleep_ms(10);
    }
    return true;
}
static void ssd_set_cursor(void) {
    bb_command(0x4e); bb_data1(0); bb_data1(0);
    bb_command(0x4f); bb_data1(0xdf); bb_data1(0x01);
}
static bool ssd_init_panel(void) {
    static const uint8_t booster[] = {0xae, 0xc7, 0xc3, 0xc0, 0x80};
    static const uint8_t gate[] = {0xdf, 0x01, 0x02};
    prepare_pins();
    x4pro_epd_reset_unhold();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(10);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(10);
    bb_command(0x12); sleep_ms(10);
    if (!ssd_ready_for("reset busy timeout")) return false;
    bb_command(0x18); bb_data1(0x80);
    bb_command(0x0c); for (size_t i = 0; i < sizeof(booster); ++i) bb_data1(booster[i]);
    bb_command(0x01); for (size_t i = 0; i < sizeof(gate); ++i) bb_data1(gate[i]);
    bb_command(0x3c); bb_data1(0x80);
    bb_command(0x11); bb_data1(0x01);
    bb_command(0x44); for (size_t i = 0; i < sizeof(ssd_x_window); ++i) bb_data1(ssd_x_window[i]);
    bb_command(0x45); for (size_t i = 0; i < sizeof(ssd_y_window); ++i) bb_data1(ssd_y_window[i]);
    if (!ssd_ready_for("pre-cursor readiness")) return false;
    ssd_set_cursor();
    bb_command(0x46); bb_data1(0xf7); if (!ssd_ready_for("first-ram busy timeout")) return false;
    bb_command(0x47); bb_data1(0xf7); return ssd_ready_for("second-ram busy timeout");
}
static bool transfer_checkpoint(uint64_t deadline_ms, unsigned work) {
    const uint64_t now = now_ms();
    if (now == UINT64_MAX || now >= deadline_ms) {
        x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
        set_reason("transfer deadline");
        return false;
    }
    transfer_work += work;
    if (transfer_work >= 1024u || now - transfer_yielded_ms >= 2u) {
        sleep_ms(1);
        transfer_yielded_ms = now;
        transfer_work = 0;
    }
    return true;
}
static void ssd_set_update_window(void) {
    if (!partial_update) {
        bb_command(0x44); for (size_t i = 0; i < sizeof(ssd_x_window); ++i) bb_data1(ssd_x_window[i]);
        bb_command(0x45); for (size_t i = 0; i < sizeof(ssd_y_window); ++i) bb_data1(ssd_y_window[i]);
        ssd_set_cursor();
        return;
    }
    const uint16_t first = (uint16_t)(X4PRO_PANEL_HEIGHT - (uint32_t)update_area.y - update_area.height);
    const uint16_t last = (uint16_t)(X4PRO_PANEL_HEIGHT - 1u - (uint32_t)update_area.y);
    const uint16_t x0 = (uint16_t)update_area.x;
    const uint16_t x1 = (uint16_t)((uint32_t)update_area.x + update_area.width - 1u);
    bb_command(0x44); bb_data1(x0 >> 8); bb_data1(x0 & 0xf8u); bb_data1(x1 >> 8); bb_data1(x1 | 7u);
    bb_command(0x45); bb_data1(first & 255u); bb_data1(first >> 8); bb_data1(last & 255u); bb_data1(last >> 8);
    bb_command(0x4e); bb_data1(x0 >> 8); bb_data1(x0 & 0xf8u);
    bb_command(0x4f); bb_data1(first & 255u); bb_data1(first >> 8);
}
