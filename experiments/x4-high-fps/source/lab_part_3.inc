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

constexpr uint8_t BAYER4[16] = {
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5,
};

inline uint8_t binary_pattern_byte(uint32_t frame, uint16_t visible_y, uint16_t byte_x,
                                   uint16_t active_y) {
  const uint16_t x0 = byte_x * 8;
  const uint16_t local_y = visible_y - active_y;
  uint8_t value = 0xFF;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const uint16_t x = x0 + bit;
    const uint16_t shifted_x = static_cast<uint16_t>((x + frame * 24U) % PANEL_WIDTH);
    const bool checker = (((shifted_x / 32U) + (local_y / 24U)) & 1U) == 0;
    const bool marker = local_y < 16U && ((x / 16U) & 1U) == ((frame / 2U) & 1U);
    if (checker ^ marker) value &= static_cast<uint8_t>(~(0x80U >> bit));
  }
  return value;
}

inline uint8_t bayer_pattern_byte(uint32_t frame, uint16_t visible_y, uint16_t byte_x,
                                  uint16_t active_y) {
  const uint16_t x0 = byte_x * 8;
  const uint16_t local_y = visible_y - active_y;
  const uint8_t phase_x = static_cast<uint8_t>(frame & 3U);
  const uint8_t phase_y = static_cast<uint8_t>((frame * 3U) & 3U);
  uint8_t value = 0xFF;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const uint16_t x = x0 + bit;
    const uint16_t shifted_x = static_cast<uint16_t>((x + frame * 12U) % PANEL_WIDTH);
    const uint8_t ramp = static_cast<uint8_t>((static_cast<uint32_t>(shifted_x) * 32U) / PANEL_WIDTH);
    const uint8_t density = ramp < 16U ? ramp : static_cast<uint8_t>(31U - ramp);
    const uint8_t threshold = BAYER4[(((visible_y + phase_y) & 3U) * 4U) + ((x + phase_x) & 3U)];
    bool black = density > threshold;
    if (local_y < 16U) black = (((shifted_x / 16U) + frame) & 1U) == 0;
    if (black) value &= static_cast<uint8_t>(~(0x80U >> bit));
  }
  return value;
}

void render_window_frame(uint8_t* destination, uint32_t frame, uint16_t active_y, uint16_t active_h,
                         Pattern pattern) {
  for (uint16_t local_y = 0; local_y < active_h; ++local_y) {
    const uint16_t visible_y = active_y + local_y;
    uint8_t* row = destination + static_cast<size_t>(local_y) * ROW_BYTES;
    if (pattern == Pattern::BinaryGrid) {
      for (uint16_t x = 0; x < ROW_BYTES; ++x) row[x] = binary_pattern_byte(frame, visible_y, x, active_y);
    } else {
      for (uint16_t x = 0; x < ROW_BYTES; ++x) row[x] = bayer_pattern_byte(frame, visible_y, x, active_y);
    }
  }
}

bool stream_plane(uint8_t command, uint32_t frame, uint16_t active_y, uint16_t active_h, Pattern pattern) {
  if (!write_command(command)) return false;
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  if (abort_requested) {
    gpio_set_level(PIN_EPD_CS, 1);
    check_abort();
  }

  const bool binary = pattern == Pattern::BinaryGrid;
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
        if (visible_y < active_y || visible_y >= active_y + active_h) {
          memset(destination, 0xFF, ROW_BYTES);
        } else if (binary) {
          for (uint16_t x = 0; x < ROW_BYTES; ++x) {
            destination[x] = binary_pattern_byte(frame, visible_y, x, active_y);
          }
        } else {
          for (uint16_t x = 0; x < ROW_BYTES; ++x) {
            destination[x] = bayer_pattern_byte(frame, visible_y, x, active_y);
          }
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

void release_frame_cache() {
  if (frame_cache) heap_caps_free(frame_cache);
  frame_cache = nullptr;
  frame_cache_stride = 0;
  frame_cache_count = 0;
}

bool prepare_frame_cache(const TestMode& mode) {
  release_frame_cache();
  if (mode.transport != Transport::CachedWindow) return true;
  frame_cache_stride = static_cast<size_t>(mode.window_h) * ROW_BYTES;
  frame_cache_count = static_cast<size_t>(mode.frame_count) + 1U;
  const size_t total = frame_cache_stride * frame_cache_count;
  frame_cache = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frame_cache) frame_cache = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_8BIT));
  if (!frame_cache) {
    Serial.printf("[X4LAB] frame cache allocation failed bytes=%lu\n", static_cast<unsigned long>(total));
    return false;
  }
  Serial.printf("[X4LAB] precomputing %u frames, %lu bytes each, total=%lu bytes\n",
                static_cast<unsigned>(frame_cache_count), static_cast<unsigned long>(frame_cache_stride),
                static_cast<unsigned long>(total));
  for (size_t frame = 0; frame < frame_cache_count; ++frame) {
    check_abort();
    render_window_frame(frame_cache + frame * frame_cache_stride, static_cast<uint32_t>(frame),
                        mode.window_y, mode.window_h, mode.pattern);
    if ((frame & 3U) == 3U) delay(1);
  }
  return true;
}

const uint8_t* cached_frame(uint32_t frame) {
  if (!frame_cache || frame >= frame_cache_count) return nullptr;
  return frame_cache + static_cast<size_t>(frame) * frame_cache_stride;
}

bool stream_cached_plane(uint8_t command, const uint8_t* data, size_t length) {
  if (!data || length == 0 || !write_command(command)) return false;
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  size_t offset = 0;
  while (offset < length) {
    check_abort();
    const size_t count = std::min(length - offset, SPI_CHUNK_BYTES);
    memcpy(dma_buffer, data + offset, count);
    if (!spi_transmit(dma_buffer, count)) {
      gpio_set_level(PIN_EPD_CS, 1);
      return false;
    }
    offset += count;
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
