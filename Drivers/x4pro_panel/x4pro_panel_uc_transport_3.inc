static bool uc_reset_and_init(void) {
    x4pro_epd_reset_unhold();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(50);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(50);
    uint64_t now = now_ms();
    if (now == UINT64_MAX || !uc_wait_idle_high(now + 1000u, "uc reset busy timeout")) return false;
    if (!uc_spi_init()) return false;
    const uint8_t psr[] = {0x37, 0x4d};
    const uint8_t resolution[] = {0x03, 0x20, 0x02, 0x58}; /* fixed 800x600 */
    const uint8_t gate_start[] = {0, 0, 0, 0};
    if (!uc_register(0x00, psr, sizeof(psr)) ||
        !uc_register(0x61, resolution, sizeof(resolution)) ||
        !uc_register(0x65, gate_start, sizeof(gate_start)) ||
        !uc_register1(0x03, 0x20) || !uc_register1(0x30, 0x0e) ||
        !uc_register1(0xe1, 0x02)) return false;
    uc_state = UC_STATE_UNKNOWN;
    uc_powered = false;
    return true;
}
