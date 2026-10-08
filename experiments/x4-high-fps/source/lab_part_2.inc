  uint8_t confirm_flag = 0;
  uint8_t confirm_version[5]{};
  read_controller_register(0x71, &flag, 1);
  read_controller_register(0x70, version, sizeof(version));
  delay(20);
  read_controller_register(0x71, &confirm_flag, 1);
  read_controller_register(0x70, confirm_version, sizeof(confirm_version));

  Serial.printf("[X4LAB] controller FLG=%02x VER=%02x %02x %02x %02x %02x BUSY_N=%d\n", flag, version[0],
                version[1], version[2], version[3], version[4], gpio_get_level(PIN_EPD_BUSY));
  if (flag != confirm_flag || memcmp(version, confirm_version, sizeof(version)) != 0) {
    Serial.println("[X4LAB] controller probe was not repeatable");
    return false;
  }
  panel_lut_id = version[2];
  if ((flag & 1U) == 0 || (panel_lut_id != REQUIRED_LUT_ID_A && panel_lut_id != REQUIRED_LUT_ID_B)) {
    Serial.printf("[X4LAB] REFUSING TEST: expected UC8279 ZHX LUT id 0x68/0x69, got 0x%02x\n", panel_lut_id);
    return false;
  }
  return true;
}

bool spi_transmit(const uint8_t* data, size_t length) {
  check_abort();
  if (!spi_device || !data || length == 0) return false;
  spi_transaction_t transaction{};
  transaction.length = length * 8;
  transaction.tx_buffer = data;
  const esp_err_t err = spi_device_polling_transmit(spi_device, &transaction);
  if (err != ESP_OK) {
    Serial.printf("[X4LAB] SPI transmit failed: %s\n", esp_err_to_name(err));
    return false;
  }
  check_abort();
  return true;
}

bool spi_transmit_byte(uint8_t value) {
  check_abort();
  spi_transaction_t transaction{};
  transaction.flags = SPI_TRANS_USE_TXDATA;
  transaction.length = 8;
  transaction.tx_data[0] = value;
  const esp_err_t err = spi_device_polling_transmit(spi_device, &transaction);
  if (err != ESP_OK) {
    Serial.printf("[X4LAB] SPI byte failed: %s\n", esp_err_to_name(err));
    return false;
  }
  check_abort();
  return true;
}

bool write_command(uint8_t command) {
  check_abort();
  gpio_set_level(PIN_EPD_DC, 0);
  gpio_set_level(PIN_EPD_CS, 0);
  if (abort_requested) {
    gpio_set_level(PIN_EPD_CS, 1);
    check_abort();
  }
  const bool ok = spi_transmit_byte(command);
  gpio_set_level(PIN_EPD_CS, 1);
  return ok;
}

bool write_data(const uint8_t* data, size_t length) {
  check_abort();
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  if (abort_requested) {
    gpio_set_level(PIN_EPD_CS, 1);
    check_abort();
  }
  size_t offset = 0;
  bool ok = true;
  while (offset < length && ok) {
    const size_t count = std::min(length - offset, SPI_CHUNK_BYTES);
    memcpy(dma_buffer, data + offset, count);
    ok = spi_transmit(dma_buffer, count);
    offset += count;
  }
  gpio_set_level(PIN_EPD_CS, 1);
  return ok;
}

bool write_data_byte(uint8_t value) {
  check_abort();
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  if (abort_requested) {
    gpio_set_level(PIN_EPD_CS, 1);
    check_abort();
  }
  const bool ok = spi_transmit_byte(value);
  gpio_set_level(PIN_EPD_CS, 1);
  return ok;
}

bool write_register(uint8_t command, const uint8_t* data, size_t length) {
  return write_command(command) && write_data(data, length);
}

bool write_register1(uint8_t command, uint8_t value) {
  return write_command(command) && write_data_byte(value);
}

bool initialize_spi() {
  spi_bus_config_t bus{};
  bus.mosi_io_num = PIN_EPD_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = PIN_EPD_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = SPI_CHUNK_BYTES;

  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.printf("[X4LAB] spi_bus_initialize failed: %s\n", esp_err_to_name(err));
    return false;
  }

  spi_device_interface_config_t device{};
  device.clock_speed_hz = SPI_CLOCK_HZ;
  device.mode = 0;
  device.spics_io_num = -1;
  device.queue_size = 1;
  err = spi_bus_add_device(SPI2_HOST, &device, &spi_device);
  if (err != ESP_OK) {
    Serial.printf("[X4LAB] spi_bus_add_device failed: %s\n", esp_err_to_name(err));
    return false;
  }

  dma_buffer = static_cast<uint8_t*>(heap_caps_malloc(SPI_CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!dma_buffer) {
    Serial.println("[X4LAB] DMA buffer allocation failed");
    return false;
  }
  return true;
}

bool controller_init(uint8_t pll) {
  gpio_set_level(PIN_EPD_RST, 0);
  delay(50);
  check_abort();
  gpio_set_level(PIN_EPD_RST, 1);
  check_abort();
  delay(50);
  check_abort();
  if (!wait_busy_high(1000, "controller reset")) return false;

  const uint8_t psr[] = {PSR_EXTERNAL, PSR_1};
  const uint8_t resolution[] = {static_cast<uint8_t>(PANEL_WIDTH >> 8), static_cast<uint8_t>(PANEL_WIDTH),
                                static_cast<uint8_t>(CONTROLLER_HEIGHT >> 8),
                                static_cast<uint8_t>(CONTROLLER_HEIGHT)};
  const uint8_t gate_start[] = {0, 0, 0, 0};
  if (!write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) ||
