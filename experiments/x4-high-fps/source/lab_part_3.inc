      !write_register(CMD_RESOLUTION, resolution, sizeof(resolution)) ||
      !write_register(CMD_GATE_SOURCE_START, gate_start, sizeof(gate_start)) ||
      !write_register1(CMD_PFS, PFS) || !write_register1(CMD_PLL, pll) ||
      !write_register1(CMD_GATE_SCAN, GATE_SCAN)) {
    return false;
  }
  screen_powered = false;
  return true;
}

bool wait_busy_cycle(uint32_t timeout_ms, const char* stage, uint32_t* duration_us) {
  const int64_t start = esp_timer_get_time();
  const int64_t assert_deadline = start + static_cast<int64_t>(BUSY_ASSERT_TIMEOUT_MS) * 1000;
  while (gpio_get_level(PIN_EPD_BUSY) != 0) {
    check_abort();
    if (esp_timer_get_time() >= assert_deadline) {
      Serial.printf("[X4LAB] BUSY_N never asserted: %s\n", stage);
      return false;
    }
    delayMicroseconds(50);
  }
  const int64_t completion_deadline = start + static_cast<int64_t>(timeout_ms) * 1000;
  while (gpio_get_level(PIN_EPD_BUSY) == 0) {
    check_abort();
    if (esp_timer_get_time() >= completion_deadline) {
      Serial.printf("[X4LAB] BUSY_N completion timeout: %s\n", stage);
      return false;
    }
    delayMicroseconds(100);
  }
  if (duration_us) *duration_us = static_cast<uint32_t>(esp_timer_get_time() - start);
  return true;
}

bool power_on() {
  if (screen_powered) return true;
  if (!write_command(CMD_POWER_ON)) return false;
  if (!wait_busy_cycle(POWER_TIMEOUT_MS, "power on", nullptr)) return false;
  screen_powered = true;
  return true;
}

bool wait_refresh(uint32_t timeout_ms, uint32_t* duration_us) {
  return wait_busy_cycle(timeout_ms, "display refresh", duration_us);
}

uint8_t pattern_byte(uint32_t frame, uint16_t visible_y, uint16_t byte_x, uint16_t active_y, uint16_t active_h) {
  if (visible_y < active_y || visible_y >= active_y + active_h) return 0xFF;
  const uint16_t x0 = byte_x * 8;
  uint8_t value = 0xFF;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const uint16_t x = x0 + bit;
    const uint16_t shifted_x = static_cast<uint16_t>((x + frame * 24U) % PANEL_WIDTH);
    const bool checker = (((shifted_x / 32U) + ((visible_y - active_y) / 24U)) & 1U) == 0;
    const bool marker = visible_y < active_y + 16U && ((x / 16U) & 1U) == ((frame / 2U) & 1U);
    if (checker ^ marker) value &= static_cast<uint8_t>(~(0x80U >> bit));
  }
  return value;
}

bool stream_plane(uint8_t command, uint32_t frame, uint16_t active_y, uint16_t active_h) {
  if (!write_command(command)) return false;
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  if (abort_requested) {
    gpio_set_level(PIN_EPD_CS, 1);
    check_abort();
  }

  uint16_t controller_row = 0;
  while (controller_row < CONTROLLER_HEIGHT) {
    check_abort();
    const uint16_t rows = std::min<uint16_t>(SPI_ROWS_PER_CHUNK, CONTROLLER_HEIGHT - controller_row);
    for (uint16_t local_row = 0; local_row < rows; ++local_row) {
      const uint16_t row = controller_row + local_row;
      uint8_t* destination = dma_buffer + static_cast<size_t>(local_row) * ROW_BYTES;
      if (row < GATE_OFFSET || row >= GATE_OFFSET + PANEL_HEIGHT) {
        memset(destination, 0xFF, ROW_BYTES);
      } else {
        const uint16_t visible_y = row - GATE_OFFSET;
        for (uint16_t x = 0; x < ROW_BYTES; ++x) {
          destination[x] = pattern_byte(frame, visible_y, x, active_y, active_h);
        }
      }
    }
    if (!spi_transmit(dma_buffer, static_cast<size_t>(rows) * ROW_BYTES)) {
      gpio_set_level(PIN_EPD_CS, 1);
      return false;
    }
    controller_row += rows;
  }
  gpio_set_level(PIN_EPD_CS, 1);
  return true;
}

bool stream_white_plane(uint8_t command) {
  if (!write_command(command)) return false;
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  memset(dma_buffer, 0xFF, SPI_CHUNK_BYTES);
  size_t remaining = static_cast<size_t>(CONTROLLER_HEIGHT) * ROW_BYTES;
  while (remaining > 0) {
    check_abort();
    const size_t count = std::min(remaining, SPI_CHUNK_BYTES);
    if (!spi_transmit(dma_buffer, count)) {
      gpio_set_level(PIN_EPD_CS, 1);
      return false;
    }
    remaining -= count;
  }
  gpio_set_level(PIN_EPD_CS, 1);
  return true;
}

bool set_partial_window(uint16_t y, uint16_t height) {
  if (height == 0 || y + height > PANEL_HEIGHT) return false;
  const uint16_t x_end = PANEL_WIDTH - 1;
  const uint16_t y_start = GATE_OFFSET + y;
  const uint16_t y_end = y_start + height - 1;
  const uint8_t window[] = {0x00,
                            0x00,
                            static_cast<uint8_t>(x_end >> 8),
                            static_cast<uint8_t>(x_end | 0x07),
                            static_cast<uint8_t>(y_start >> 8),
                            static_cast<uint8_t>(y_start),
                            static_cast<uint8_t>(y_end >> 8),
                            static_cast<uint8_t>(y_end),
                            0x01};
