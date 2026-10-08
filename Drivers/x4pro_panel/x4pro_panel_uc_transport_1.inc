/* ---------------- UC8279 fixed 20 MHz native FSPI transport ---------------- */
static void iomux_function(uint32_t pin, uint32_t function) {
    uint32_t mux = x4pro_reg_read(x4pro_iomux_reg(pin));
    mux &= ~(7u << 12);
    mux |= function << 12;
    x4pro_reg_write(x4pro_iomux_reg(pin), mux);
}
static bool spi2_wait_clear(uint32_t mask) {
    for (uint32_t spins = 0; spins < 200000u; ++spins)
        if ((x4pro_reg_read(SPI2_CMD_REG) & mask) == 0) return true;
    set_reason("spi2 timeout");
    return false;
}
static bool uc_spi_init(void) {
    if (uc_spi_ready) return true;
    uint32_t reg = x4pro_reg_read(SYSTEM_PERIP_CLK_EN0_REG);
    x4pro_reg_write(SYSTEM_PERIP_CLK_EN0_REG, reg | SYSTEM_SPI2_BIT);
    reg = x4pro_reg_read(SYSTEM_PERIP_RST_EN0_REG);
    x4pro_reg_write(SYSTEM_PERIP_RST_EN0_REG, reg | SYSTEM_SPI2_BIT);
    x4pro_reg_write(SYSTEM_PERIP_RST_EN0_REG, reg & ~SYSTEM_SPI2_BIT);
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, false);
    x4pro_pin_output(X4PRO_PIN_EPD_SCLK, false);
    iomux_function(X4PRO_PIN_EPD_MOSI, 4u); /* FSPID */
    iomux_function(X4PRO_PIN_EPD_SCLK, 4u); /* FSPICLK */
    x4pro_reg_write(SPI2_CTRL_REG, 0u);
    x4pro_reg_write(SPI2_CLOCK_REG, SPI2_CLOCK_20MHZ);
    x4pro_reg_write(SPI2_USER_REG, SPI2_USER_MOSI);
    x4pro_reg_write(SPI2_USER1_REG, 0u);
    x4pro_reg_write(SPI2_USER2_REG, 0u);
    x4pro_reg_write(SPI2_MISC_REG, 0u); /* mode 0, idle low */
    x4pro_reg_write(SPI2_CMD_REG, SPI2_CMD_UPDATE);
    if (!spi2_wait_clear(SPI2_CMD_UPDATE)) return false;
    uc_spi_ready = true;
    return true;
}
static void uc_spi_release(void) {
    if (!uc_spi_ready) return;
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    iomux_function(X4PRO_PIN_EPD_MOSI, 1u);
    iomux_function(X4PRO_PIN_EPD_SCLK, 1u);
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, false);
    x4pro_pin_output(X4PRO_PIN_EPD_SCLK, false);
    uc_spi_ready = false;
}
static bool uc_spi_chunk(const uint8_t *data, size_t length) {
    if (!uc_spi_ready || !data || !length || length > UC_SPI_CHUNK) return false;
    for (size_t word = 0; word < 16u; ++word) {
        uint32_t value = 0;
        const size_t base = word * 4u;
        for (size_t b = 0; b < 4u; ++b) {
            const size_t i = base + b;
            if (i < length) value |= (uint32_t)data[i] << (8u * b);
        }
        x4pro_reg_write(SPI2_W0_REG + (uint32_t)word * 4u, value);
    }
    x4pro_reg_write(SPI2_MS_DLEN_REG, (uint32_t)(length * 8u - 1u));
    x4pro_reg_write(SPI2_CMD_REG, SPI2_CMD_UPDATE);
    if (!spi2_wait_clear(SPI2_CMD_UPDATE)) return false;
    x4pro_reg_write(SPI2_CMD_REG, SPI2_CMD_USR);
    return spi2_wait_clear(SPI2_CMD_USR);
}
