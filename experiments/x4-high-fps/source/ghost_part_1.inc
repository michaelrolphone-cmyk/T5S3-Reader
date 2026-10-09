
      if (black) row[x >> 3] &= static_cast<uint8_t>(~(0x80U >> (x & 7U)));
    }
  }
}

bool ghost_allocate_frames() {
  if (ghost_pattern && ghost_inverse && ghost_white) return true;
  auto allocate = []() -> uint8_t* {
    uint8_t* p = static_cast<uint8_t*>(heap_caps_malloc(GHOST_VISIBLE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!p) p = static_cast<uint8_t*>(heap_caps_malloc(GHOST_VISIBLE_BYTES, MALLOC_CAP_8BIT));
    return p;
  };
  ghost_pattern = allocate();
  ghost_inverse = allocate();
  ghost_white = allocate();
  if (!ghost_pattern || !ghost_inverse || !ghost_white) {
    Serial.printf("[X4LAB] frame allocation failed pattern=%p inverse=%p white=%p bytes=%lu\n",
                  ghost_pattern, ghost_inverse, ghost_white,
                  static_cast<unsigned long>(GHOST_VISIBLE_BYTES));
    return false;
  }
  ghost_render_pattern(ghost_pattern);
  memset(ghost_white, 0xFF, GHOST_VISIBLE_BYTES);
  for (size_t i = 0; i < GHOST_VISIBLE_BYTES; ++i) ghost_inverse[i] = static_cast<uint8_t>(~ghost_pattern[i]);
  Serial.printf("[X4LAB] frames ready bytes=%lu pattern_hash=%08lx inverse_hash=%08lx white_hash=%08lx\n",
                static_cast<unsigned long>(GHOST_VISIBLE_BYTES),
                static_cast<unsigned long>(ghost_hash(ghost_pattern, GHOST_VISIBLE_BYTES)),
                static_cast<unsigned long>(ghost_hash(ghost_inverse, GHOST_VISIBLE_BYTES)),
                static_cast<unsigned long>(ghost_hash(ghost_white, GHOST_VISIBLE_BYTES)));
  return true;
}

void ghost_poll_abort_only() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'q' || c == 'Q' || c == 'x' || c == 'X' || c == 27) {
      abort_pin = 0xFE;
      abort_requested = true;
      check_abort();
    }
    if (c == 's' || c == 'S') soft_stop_requested = true;
  }
}

bool ghost_stop_requested() {
  check_abort();
  ghost_poll_abort_only();
  if (gpio_get_level(PIN_BTN_POWER) == 0) soft_stop_requested = true;
  return soft_stop_requested;
}

void ghost_wait_power_release() {
  while (gpio_get_level(PIN_BTN_POWER) == 0) {
    check_abort();
    ghost_poll_abort_only();
    delay(10);
  }
}

bool ghost_power_on_timed(const char* reason, uint32_t* measured_us = nullptr) {
  if (screen_powered) {
    if (measured_us) *measured_us = 0;
    Serial.printf("[X4LAB] PON skipped reason=%s already_powered=1\n", reason);
    return true;
  }
  const int64_t started = esp_timer_get_time();
  uint32_t busy_us = 0;
  if (!write_command(CMD_POWER_ON) || !wait_busy_cycle(POWER_TIMEOUT_MS, "power on", &busy_us)) return false;
  screen_powered = true;
  const uint32_t total_us = static_cast<uint32_t>(esp_timer_get_time() - started);
  if (measured_us) *measured_us = total_us;
  Serial.printf("[X4LAB] PON reason=%s busy=%lu us total=%lu us\n", reason,
                static_cast<unsigned long>(busy_us), static_cast<unsigned long>(total_us));
  return true;
}

bool ghost_power_off_timed(const char* reason, uint32_t* measured_us = nullptr) {
  if (!screen_powered) {
    if (measured_us) *measured_us = 0;
    Serial.printf("[X4LAB] POF skipped reason=%s already_powered=0\n", reason);
    return true;
  }
  const int64_t started = esp_timer_get_time();
  uint32_t busy_us = 0;
  if (!write_command(GHOST_CMD_POWER_OFF) || !wait_busy_cycle(POWER_TIMEOUT_MS, "power off", &busy_us)) return false;
  screen_powered = false;
  const uint32_t total_us = static_cast<uint32_t>(esp_timer_get_time() - started);
  if (measured_us) *measured_us = total_us;
  Serial.printf("[X4LAB] POF reason=%s busy=%lu us total=%lu us\n", reason,
                static_cast<unsigned long>(busy_us), static_cast<unsigned long>(total_us));
  return true;
}

bool ghost_stream_full_controller_plane(uint8_t command, const uint8_t* visible) {
  if (!visible || !write_command(command)) return false;
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  uint16_t controller_row = 0;
  while (controller_row < CONTROLLER_HEIGHT) {
    check_abort();
    const uint16_t rows = std::min<uint16_t>(SPI_ROWS_PER_CHUNK, CONTROLLER_HEIGHT - controller_row);
    for (uint16_t local = 0; local < rows; ++local) {
      const uint16_t row = controller_row + local;
      uint8_t* target = dma_buffer + static_cast<size_t>(local) * ROW_BYTES;
      if (row < GATE_OFFSET || row >= GATE_OFFSET + PANEL_HEIGHT) {
        memset(target, 0xFF, ROW_BYTES);
      } else {
        memcpy(target, visible + static_cast<size_t>(row - GATE_OFFSET) * ROW_BYTES, ROW_BYTES);
      }
