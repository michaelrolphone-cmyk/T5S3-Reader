    return false;
  }
  for (size_t i = 0; i < focused_cache_count; ++i) {
    focused_render_frame(focused_cache + i * FOCUSED_STRIDE, static_cast<uint32_t>(i), mode);
    if ((i & 3U) == 3U) delay(1);
  }
  Serial.printf("[X4LAB] cached=%u stride=%lu\n", static_cast<unsigned>(focused_cache_count),
                static_cast<unsigned long>(FOCUSED_STRIDE));
  return true;
}

const uint8_t *focused_frame(uint32_t frame) {
  if (!focused_cache || frame >= focused_cache_count) return nullptr;
  return focused_cache + static_cast<size_t>(frame) * FOCUSED_STRIDE;
}

FocusRect focused_dirty_rect(const FocusMode &mode, uint32_t old_frame, uint32_t new_frame) {
  if (mode.pattern == FocusPattern::Density) {
    const uint16_t width = std::min<uint16_t>(mode.transition_width, PANEL_WIDTH);
    return {static_cast<uint16_t>((PANEL_WIDTH - width) / 2U), width};
  }
  const uint16_t old_x = focused_position_x(old_frame);
  const uint16_t new_x = focused_position_x(new_frame);
  const uint16_t left = std::min<uint16_t>(old_x, new_x);
  const uint16_t right = static_cast<uint16_t>(std::max<uint16_t>(old_x, new_x) + FOCUSED_TARGET_W);
  return {left, static_cast<uint16_t>(right - left)};
}

bool focused_set_partial_rect(uint16_t x, uint16_t width) {
  if (width == 0 || (x & 7U) != 0 || (width & 7U) != 0 || x + width > PANEL_WIDTH) return false;
  const uint16_t x_end = static_cast<uint16_t>(x + width - 1U);
  const uint16_t y_start = static_cast<uint16_t>(GATE_OFFSET + FOCUSED_Y);
  const uint16_t y_end = static_cast<uint16_t>(y_start + FOCUSED_H - 1U);
  const uint8_t window[] = {
      static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xF8U),
      static_cast<uint8_t>(x_end >> 8), static_cast<uint8_t>(x_end | 0x07U),
      static_cast<uint8_t>(y_start >> 8), static_cast<uint8_t>(y_start),
      static_cast<uint8_t>(y_end >> 8), static_cast<uint8_t>(y_end), 0x01};
  return write_command(CMD_PARTIAL_IN) && write_register(CMD_PARTIAL_WINDOW, window, sizeof(window));
}

bool focused_stream_rect_plane(uint8_t command, uint32_t frame, uint16_t x, uint16_t width) {
  const uint8_t *data = focused_frame(frame);
  if (!data || !focused_set_partial_rect(x, width) || !write_command(command)) return false;
  const size_t row_bytes = width / 8U;
  const size_t source_x = x / 8U;
  const size_t rows_per_chunk = std::max<size_t>(1U, SPI_CHUNK_BYTES / row_bytes);
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  uint16_t row = 0;
  while (row < FOCUSED_H) {
    check_abort();
    const uint16_t rows = static_cast<uint16_t>(std::min<size_t>(rows_per_chunk, FOCUSED_H - row));
    for (uint16_t local = 0; local < rows; ++local) {
      memcpy(dma_buffer + static_cast<size_t>(local) * row_bytes,
             data + static_cast<size_t>(row + local) * ROW_BYTES + source_x, row_bytes);
    }
    if (!spi_transmit(dma_buffer, static_cast<size_t>(rows) * row_bytes)) {
      gpio_set_level(PIN_EPD_CS, 1);
      return false;
    }
    row = static_cast<uint16_t>(row + rows);
  }
  gpio_set_level(PIN_EPD_CS, 1);
  return write_command(CMD_PARTIAL_OUT);
}

bool focused_stream_full_plane(uint8_t command, const uint8_t *visible) {
  if (!visible || !write_command(command)) return false;
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  uint16_t row = 0;
  while (row < CONTROLLER_HEIGHT) {
    check_abort();
    const uint16_t rows = std::min<uint16_t>(SPI_ROWS_PER_CHUNK, CONTROLLER_HEIGHT - row);
    for (uint16_t local = 0; local < rows; ++local) {
      const uint16_t controller_y = static_cast<uint16_t>(row + local);
      uint8_t *target = dma_buffer + static_cast<size_t>(local) * ROW_BYTES;
      if (controller_y < GATE_OFFSET + FOCUSED_Y || controller_y >= GATE_OFFSET + FOCUSED_Y + FOCUSED_H)
        memset(target, 0xFF, ROW_BYTES);
      else
        memcpy(target, visible + static_cast<size_t>(controller_y - GATE_OFFSET - FOCUSED_Y) * ROW_BYTES,
               ROW_BYTES);
    }
    if (!spi_transmit(dma_buffer, static_cast<size_t>(rows) * ROW_BYTES)) {
      gpio_set_level(PIN_EPD_CS, 1);
      return false;
    }
    row = static_cast<uint16_t>(row + rows);
  }
  gpio_set_level(PIN_EPD_CS, 1);
  return true;
}

bool focused_power_off() {
  if (!screen_powered) return true;
  if (!write_command(F_CMD_POWER_OFF)) return false;
  uint32_t elapsed = 0;
  if (!wait_busy_cycle(POWER_TIMEOUT_MS, "focused power off", &elapsed)) return false;
  screen_powered = false;
  Serial.printf("[X4LAB] POF=%lu us\n", static_cast<unsigned long>(elapsed));
  return true;
}

bool focused_write_power_profile(const FocusMode &mode) {
  const uint8_t power[] = {0x07, 0x17, mode.vdh, mode.vdl, VDHR_3V0};
  return write_register(F_CMD_POWER_SETTING, power, sizeof(power)) &&
         write_register1(F_CMD_TCON, mode.tcon) && write_register1(CMD_PLL, PLL_FAST) &&
         write_register1(CMD_VCOM_DATA_INTERVAL, mode.cdi);
}
