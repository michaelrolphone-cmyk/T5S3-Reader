  for (uint16_t y = top0; y < top0 + 28; ++y) {
    uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
    for (uint16_t x = left; x < left + square_w; ++x) {
      bool black = true;
      if (mode.pattern == VisualPattern::FineEdges)
        black = (((x - left) + (y - top0)) & 1U) == 0;
      set_pixel(row, x, black);
    }
  }
  for (uint16_t y = top1; y < top1 + 28; ++y) {
    uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
    for (uint16_t x = left; x < left + square_w; ++x) {
      bool white = true;
      if (mode.pattern == VisualPattern::FineEdges)
        white = (((x - left) + (y - top1)) & 1U) == 0;
      if (white) set_pixel(row, x, false);
    }
  }
}

bool visual_prepare_cache(const VisualMode &mode) {
  visual_release_cache();
  visual_cache_count = static_cast<size_t>(mode.frames) + 1U;
  const size_t bytes = VISUAL_STRIDE * visual_cache_count;
  visual_cache = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!visual_cache) visual_cache = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  if (!visual_cache) {
    Serial.printf("[X4LAB] visual cache allocation failed bytes=%lu\n", static_cast<unsigned long>(bytes));
    return false;
  }
  for (size_t i = 0; i < visual_cache_count; ++i) {
    render_visual_frame(visual_cache + i * VISUAL_STRIDE, static_cast<uint32_t>(i), mode);
    if ((i & 3U) == 3U) delay(1);
  }
  Serial.printf("[X4LAB] cached %u visual frames, %lu bytes each\n",
                static_cast<unsigned>(visual_cache_count), static_cast<unsigned long>(VISUAL_STRIDE));
  return true;
}

const uint8_t *visual_frame(uint32_t frame) {
  if (!visual_cache || frame >= visual_cache_count) return nullptr;
  return visual_cache + static_cast<size_t>(frame) * VISUAL_STRIDE;
}

bool visual_stream_full_plane(uint8_t command, const uint8_t *visible) {
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
      if (controller_y < GATE_OFFSET + VISUAL_Y || controller_y >= GATE_OFFSET + VISUAL_Y + VISUAL_H)
        memset(target, 0xFF, ROW_BYTES);
      else
        memcpy(target, visible + static_cast<size_t>(controller_y - GATE_OFFSET - VISUAL_Y) * ROW_BYTES,
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

bool visual_stream_window_plane(uint8_t command, uint32_t frame) {
  const uint8_t *data = visual_frame(frame);
  if (!data || !set_partial_window(VISUAL_Y, VISUAL_H) ||
      !stream_cached_plane(command, data, VISUAL_STRIDE)) return false;
  return write_command(CMD_PARTIAL_OUT);
}

bool visual_power_off() {
  if (!screen_powered) return true;
  if (!write_command(V_CMD_POWER_OFF)) return false;
  uint32_t elapsed = 0;
  if (!wait_busy_cycle(POWER_TIMEOUT_MS, "visual power off", &elapsed)) return false;
  screen_powered = false;
  Serial.printf("[X4LAB] POF=%lu us\n", static_cast<unsigned long>(elapsed));
  return true;
}

bool visual_write_power_profile(const VisualMode &mode) {
  const uint8_t power[] = {0x07, 0x17, mode.vdh, mode.vdl, mode.vdhr};
  if (!write_register(V_CMD_POWER_SETTING, power, sizeof(power))) return false;
  if (mode.vcom != VCOM_FACTORY && !write_register1(V_CMD_VCOM_DC, mode.vcom)) return false;
  return write_register1(V_CMD_TCON, mode.tcon) && write_register1(CMD_PLL, mode.pll) &&
         write_register1(CMD_VCOM_DATA_INTERVAL, mode.cdi);
}

bool visual_clean_baseline(const VisualMode &mode) {
  Serial.println("[X4LAB] establishing common OTP baseline");
  const uint8_t *frame0 = visual_frame(0);
  if (!frame0 || !controller_init(PLL_STOCK)) return false;
