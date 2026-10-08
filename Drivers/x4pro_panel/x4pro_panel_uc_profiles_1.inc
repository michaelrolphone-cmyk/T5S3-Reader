static bool uc_set_window(const risc_display_rect_v1 *r) {
    if (!r || r->x < 0 || r->y < 0 || !r->width || !r->height) return false;
    const uint16_t left = (uint16_t)r->x;
    const uint16_t right = (uint16_t)((uint32_t)r->x + r->width - 1u);
    const uint16_t top = (uint16_t)((uint32_t)r->y + UC_VISIBLE_OFFSET);
    const uint16_t bottom = (uint16_t)(top + r->height - 1u);
    const uint8_t window[] = {
        (uint8_t)(left >> 8), (uint8_t)(left & 0xf8u),
        (uint8_t)(right >> 8), (uint8_t)(right | 0x07u),
        (uint8_t)(top >> 8), (uint8_t)top,
        (uint8_t)(bottom >> 8), (uint8_t)bottom, 0x01
    };
    return uc_command(0x91) && uc_register(0x90, window, sizeof(window));
}
static bool uc_end_window(void) { return uc_command(0x92); }
static bool uc_write_window_plane(uint8_t command, const uint8_t *pixels,
                                  const risc_display_rect_v1 *r, uint64_t deadline_ms) {
    if (!pixels || !uc_set_window(r) || !uc_command(command)) return false;
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    uint8_t chunk[UC_SPI_CHUNK];
    size_t used = 0;
    const uint32_t x_byte = (uint32_t)r->x / 8u;
    const uint32_t width_bytes = r->width / 8u;
    bool ok = true;
    for (uint32_t row = 0; row < r->height && ok; ++row) {
        const size_t base = (size_t)((uint32_t)r->y + row) * ROW_BYTES + x_byte;
        for (uint32_t col = 0; col < width_bytes; ++col) {
            chunk[used++] = (uint8_t)~pixels[base + col];
            if (used == sizeof(chunk)) {
                ok = uc_spi_stream(chunk, used, deadline_ms);
                used = 0;
            }
        }
    }
    if (ok && used) ok = uc_spi_stream(chunk, used, deadline_ms);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return ok && uc_end_window();
}
static bool uc_write_full600_plane(uint8_t command, const uint8_t *pixels,
                                   bool white_visible, uint64_t deadline_ms) {
    if (!uc_command(command)) return false;
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    uint8_t chunk[UC_SPI_CHUNK];
    size_t used = 0;
    bool ok = true;
    for (uint32_t row = 0; row < UC_CONTROLLER_ROWS && ok; ++row) {
        for (uint32_t col = 0; col < ROW_BYTES; ++col) {
            uint8_t value = 0xffu;
            if (row >= UC_VISIBLE_OFFSET && !white_visible)
                value = (uint8_t)~pixels[(size_t)(row - UC_VISIBLE_OFFSET) * ROW_BYTES + col];
            chunk[used++] = value;
            if (used == sizeof(chunk)) {
                ok = uc_spi_stream(chunk, used, deadline_ms);
                used = 0;
            }
        }
    }
    if (ok && used) ok = uc_spi_stream(chunk, used, deadline_ms);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return ok;
}
