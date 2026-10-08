static bool ssd_transfer_plane(uint8_t ram_command, uint64_t deadline_ms) {
    bb_command(ram_command);
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    const uint8_t *pixels = partial_update && ram_command == 0x26 ? previous_frame : frame;
    const unsigned x = partial_update ? (unsigned)update_area.x / 8u : 0u;
    const unsigned y = partial_update ? (unsigned)update_area.y : 0u;
    const unsigned w = partial_update ? update_area.width / 8u : ROW_BYTES;
    const unsigned h = partial_update ? update_area.height : X4PRO_PANEL_HEIGHT;
    for (unsigned row = 0; row < h; ++row) {
        for (unsigned col = 0; col < w; ++col) {
            const size_t i = (size_t)(row + y) * ROW_BYTES + x + col;
            bb_spi_byte((uint8_t)~pixels[i]);
            ++bytes_sent;
            if ((col & 63u) == 63u && !transfer_checkpoint(deadline_ms, 64u)) return false;
        }
        if (!transfer_checkpoint(deadline_ms, w & 63u)) return false;
    }
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return true;
}
static bool ssd_wait_idle(uint64_t deadline_ms) {
    bool saw_busy = false;
    while (!saw_busy) {
        const uint64_t now = now_ms();
        if (now == UINT64_MAX || now >= deadline_ms) { set_reason("busy never asserted"); return false; }
        saw_busy = x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
        if (!saw_busy) sleep_ms(10);
    }
    busy_assert_ms = now_ms();
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        const uint64_t now = now_ms();
        if (now == UINT64_MAX || now >= deadline_ms) { set_reason("busy completion timeout"); return false; }
        sleep_ms(10);
    }
    busy_done_ms = now_ms();
    return busy_done_ms != UINT64_MAX;
}
static bool ssd_transfer_frame(uint64_t deadline_ms) {
    if (!sample_now(&transfer_start_ms) || !ssd_ready_for("pre-transfer readiness")) return false;
    ssd_set_update_window();
    if (!ssd_transfer_plane(0x24, deadline_ms) || !ssd_transfer_plane(0x26, deadline_ms)) return false;
    transfer_end_ms = now_ms();
    if (transfer_end_ms == UINT64_MAX || transfer_end_ms >= deadline_ms) return false;
    busy_before = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    if (busy_before) { set_reason("busy already active"); return false; }
    bb_command(0x21); bb_data1(partial_update ? 0x00 : 0x40);
    bb_command(0x3c); bb_data1(partial_update ? 0x80 : 0xc0);
    bb_command(0x22); bb_data1(partial_update ? 0xfc : 0xf7);
    refresh_ms = now_ms();
    bb_command(0x20);
    if (!ssd_wait_idle(deadline_ms)) return false;
    copy_rect(previous_frame, frame, partial_update ? &update_area : &(risc_display_rect_v1){0,0,X4PRO_PANEL_WIDTH,X4PRO_PANEL_HEIGHT});
    previous_seeded = true;
    return true;
}

