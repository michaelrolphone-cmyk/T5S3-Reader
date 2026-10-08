static bool uc_spi_stream(const uint8_t *data, size_t length, uint64_t deadline_ms) {
    size_t offset = 0;
    while (offset < length) {
        const size_t count = length - offset > UC_SPI_CHUNK ? UC_SPI_CHUNK : length - offset;
        if (!uc_spi_chunk(data + offset, count)) return false;
        offset += count;
        bytes_sent += (uint32_t)count;
        transfer_work += (unsigned)count;
        if (transfer_work >= 4096u) {
            const uint64_t now = now_ms();
            if (now == UINT64_MAX || now >= deadline_ms) { set_reason("transfer deadline"); return false; }
            sleep_ms(1);
            transfer_yielded_ms = now;
            transfer_work = 0;
        }
    }
    return true;
}
static bool uc_command(uint8_t command) {
    x4pro_pin_level(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    const bool ok = uc_spi_chunk(&command, 1u);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return ok;
}
static bool uc_data(const uint8_t *data, size_t length) {
    const uint64_t now = now_ms();
    const uint64_t deadline = now == UINT64_MAX ? UINT64_MAX : now + 500u;
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    const bool ok = uc_spi_stream(data, length, deadline);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return ok;
}
static bool uc_data1(uint8_t value) { return uc_data(&value, 1u); }
static bool uc_register(uint8_t command, const uint8_t *data, size_t length) {
    return uc_command(command) && uc_data(data, length);
}
static bool uc_register1(uint8_t command, uint8_t value) {
    return uc_command(command) && uc_data1(value);
}

static bool uc_wait_idle_high(uint64_t deadline_ms, const char *failure) {
    while (!x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        const uint64_t now = now_ms();
        if (now == UINT64_MAX || now >= deadline_ms) { set_reason(failure); return false; }
        sleep_ms(1);
    }
    return true;
}
static bool uc_wait_refresh(uint64_t deadline_ms, uint32_t minimum_busy_ms) {
    const uint64_t start = now_ms();
    if (start == UINT64_MAX) return false;
    busy_before = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    if (!busy_before) { set_reason("busy active before drf"); return false; }
    if (!uc_command(0x12)) return false;
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        const uint64_t now = now_ms();
        if (now == UINT64_MAX || now >= deadline_ms || now - start >= UC_BUSY_ASSERT_MS) {
            set_reason("busy never asserted");
            return false;
        }
        sleep_ms(1);
    }
    busy_assert_ms = now_ms();
    if (busy_assert_ms == UINT64_MAX) return false;
    while (!x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        const uint64_t now = now_ms();
        if (now == UINT64_MAX || now >= deadline_ms) { set_reason("busy completion timeout"); return false; }
        sleep_ms(1);
    }
    busy_done_ms = now_ms();
    if (busy_done_ms == UINT64_MAX || busy_done_ms < busy_assert_ms ||
        busy_done_ms - busy_assert_ms < minimum_busy_ms) {
        set_reason("implausible busy interval");
        return false;
    }
    return true;
}
