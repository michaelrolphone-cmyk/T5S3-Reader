"""Use production GPIO helpers; substitute only the raw register boundary."""
def header(root):
    wire = (root / 'test/storage_volume/fake/x4pro_mmio.h').read_text()
    for name in ('output', 'release', 'read', 'input', 'hold'):
        wire = wire.replace('x4pro_pin_' + name, 'wire_pin_' + name)
    mmio = (root / 'Drivers/x4pro_board/x4pro_mmio.h').read_text()
    write = '*(volatile uint32_t *)address = value;'
    read = 'return *(volatile uint32_t *)address;'
    assert mmio.count(write) == mmio.count(read) == 1
    mmio = mmio.replace(write, 'test_mmio_write(address, value);')
    mmio = mmio.replace(read, 'return test_mmio_read(address);')
    return wire + r'''
extern uint64_t sd_clock_mux_writes, sd_clock_direction_writes, sd_clock_levels;
extern uint32_t sd_clock_cycles;
extern bool sd_clock_readback_stuck, sd_clock_check_phases;
static bool test_clock_level, test_prior_edge;
static uint32_t test_edge_cycles;
static uint32_t test_mux[49];
static void test_mmio_write(uint32_t address, uint32_t value) {
    if (address >= 0x60009004u && address <= 0x600090c4u) {
        unsigned pin = (address - 0x60009004u) / 4u;
        test_mux[pin] = value;
        if (pin == X4PRO_PIN_SD_CLK) ++sd_clock_mux_writes;
        return;
    }
    if (address == 0x600080d8u) { wire_pin_hold(5, (value & (1u << 5)) != 0); return; }
    if (address == 0x60004008u || address == 0x6000400cu) {
        if (value & (1u << 5)) wire_pin_output(5, address == 0x60004008u);
        return;
    }
    if (address == 0x60004014u || address == 0x60004018u) {
        for (unsigned pin = 40; pin <= 42; ++pin) {
            if (!(value & (1u << (pin - 32)))) continue;
            if (pin == X4PRO_PIN_SD_CLK) {
                ++sd_clock_levels;
                bool next = address == 0x60004014u;
                if (sd_clock_check_phases && next != test_clock_level) {
                    if (test_prior_edge && (uint32_t)(sd_clock_cycles - test_edge_cycles) < 24u) {
                        card_bad_pin = true;
                    }
                    test_prior_edge = true; test_edge_cycles = sd_clock_cycles;
                }
                test_clock_level = next;
            }
            wire_pin_output(pin, address == 0x60004014u);
        }
        return;
    }
    if (address == 0x60004030u && (value & (1u << (X4PRO_PIN_SD_CLK - 32))))
        ++sd_clock_direction_writes;
    if (address == 0x60004034u)
        for (unsigned pin = 40; pin <= 42; ++pin)
            if (value & (1u << (pin - 32))) wire_pin_release(pin);
}
static uint32_t test_mmio_read(uint32_t address) {
    if (address >= 0x60009004u && address <= 0x600090c4u)
        return test_mux[(address - 0x60009004u) / 4u];
    if (address == 0x600080d8u) return card_sleep_off ? 1u << 5 : 0;
    if (address == 0x60004040u)
        return (wire_pin_read(X4PRO_PIN_SD_CMD) ? 1u << (X4PRO_PIN_SD_CMD - 32) : 0) |
               (wire_pin_read(X4PRO_PIN_SD_DAT0) ? 1u << (X4PRO_PIN_SD_DAT0 - 32) : 0) |
               (test_clock_level && !sd_clock_readback_stuck ? 1u << (X4PRO_PIN_SD_CLK - 32) : 0);
    return 0;
}
''' + mmio
