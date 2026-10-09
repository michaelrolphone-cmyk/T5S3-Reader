  return write_command(CMD_PARTIAL_IN) && write_register(CMD_PARTIAL_WINDOW, window, sizeof(window));
}

constexpr uint8_t GHOST_CMD_POWER_OFF = 0x02;
constexpr uint32_t GHOST_SETTLE_MS = 2300;
constexpr uint32_t GHOST_OBSERVE_MS = 60000;
constexpr uint32_t GHOST_LOG_INTERVAL_MS = 5000;
constexpr uint32_t GHOST_MAINTENANCE_MS = 30000;
constexpr size_t GHOST_FRAME_BYTES = static_cast<size_t>(ROW_BYTES) * PANEL_HEIGHT;
constexpr const char* GHOST_LAB_VERSION = "0.1.7";

enum class GhostAction : uint8_t {
  PoweredIdle,
  PowerOff,
  InvertThenWhite,
  SyncDtm1,
  SyncDtm1AndPowerOff,
  MaintenanceAtThirtySeconds,
  TargetEndingCompensation,
  ResetHeld,
  PowerOffThenPowerOn,
};

struct GhostMode {
  const char* name;
  GhostAction action;
};

constexpr GhostMode GHOST_MODES[] = {
    {"powered idle: stale DTM1, no maintenance", GhostAction::PoweredIdle},
    {"POF after settling: stale DTM1", GhostAction::PowerOff},
    {"one inverted-pattern frame, one white frame, powered idle", GhostAction::InvertThenWhite},
    {"synchronize DTM1 to white, powered idle", GhostAction::SyncDtm1},
    {"synchronize DTM1 to white, then POF", GhostAction::SyncDtm1AndPowerOff},
    {"powered idle with one resident maintenance DRF at 30 seconds", GhostAction::MaintenanceAtThirtySeconds},
    {"one inverse frame, two-frame final-white compensation, powered idle", GhostAction::TargetEndingCompensation},
    {"hold controller RESET low after settling", GhostAction::ResetHeld},
    {"POF for 30 seconds, PON only, then powered idle for 30 seconds", GhostAction::PowerOffThenPowerOn},
};
constexpr size_t GHOST_MODE_COUNT = sizeof(GHOST_MODES) / sizeof(GHOST_MODES[0]);
static_assert(GHOST_MODE_COUNT == 9, "serial selection assumes modes 1 through 9");

uint8_t* ghost_pattern = nullptr;
uint8_t* ghost_inverse = nullptr;
uint8_t* ghost_white = nullptr;

void poll_mode_serial_abort() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'q' || c == 'Q' || c == 'x' || c == 'X' || c == 27) {
      abort_pin = 0xFE;
      abort_requested = true;
      check_abort();
    }
  }
}

void wait_power_release() {
  while (gpio_get_level(PIN_BTN_POWER) == 0) {
    check_abort();
    poll_mode_serial_abort();
    delay(10);
  }
}

bool observation_stop_requested() {
  check_abort();
  poll_mode_serial_abort();
  if (gpio_get_level(PIN_BTN_POWER) == 0) {
    soft_stop_requested = true;
    return true;
  }
  return false;
}

bool allocate_ghost_frames() {
  if (ghost_pattern && ghost_inverse && ghost_white) return true;
  auto allocate = []() -> uint8_t* {
    uint8_t* memory = static_cast<uint8_t*>(heap_caps_malloc(GHOST_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!memory) memory = static_cast<uint8_t*>(heap_caps_malloc(GHOST_FRAME_BYTES, MALLOC_CAP_8BIT));
    return memory;
  };
  ghost_pattern = allocate();
  ghost_inverse = allocate();
  ghost_white = allocate();
  if (!ghost_pattern || !ghost_inverse || !ghost_white) {
    Serial.printf("[X4LAB] ghost frame allocation failed; bytes_per_frame=%lu\n",
                  static_cast<unsigned long>(GHOST_FRAME_BYTES));
    return false;
  }

  memset(ghost_white, 0xFF, GHOST_FRAME_BYTES);
  for (uint16_t y = 0; y < PANEL_HEIGHT; ++y) {
    for (uint16_t byte_x = 0; byte_x < ROW_BYTES; ++byte_x) {
      uint8_t value = 0xFF;
      for (uint8_t bit = 0; bit < 8; ++bit) {
        const uint16_t x = static_cast<uint16_t>(byte_x * 8U + bit);
        bool black = false;
        if (y < PANEL_HEIGHT / 2U && x < PANEL_WIDTH / 2U) {
          black = (((x / 40U) + (y / 40U)) & 1U) == 0;
        } else if (y < PANEL_HEIGHT / 2U) {
          black = ((x / 24U) & 1U) == 0;
        } else if (x < PANEL_WIDTH / 2U) {
          black = ((y / 20U) & 1U) == 0;
        } else {
          black = ((((x - PANEL_WIDTH / 2U) / 32U) + ((y - PANEL_HEIGHT / 2U) / 24U)) & 1U) == 0;
          if ((((x + y) / 18U) & 3U) == 0) black = !black;
        }
        if (x < 8U || x >= PANEL_WIDTH - 8U || y < 8U || y >= PANEL_HEIGHT - 8U) black = true;
        if ((x >= PANEL_WIDTH / 2U - 6U && x < PANEL_WIDTH / 2U + 6U) ||
            (y >= PANEL_HEIGHT / 2U - 6U && y < PANEL_HEIGHT / 2U + 6U)) {
          black = false;
        }
        if (black) value &= static_cast<uint8_t>(~(0x80U >> bit));
      }
      const size_t offset = static_cast<size_t>(y) * ROW_BYTES + byte_x;
      ghost_pattern[offset] = value;
      ghost_inverse[offset] = static_cast<uint8_t>(~value);
    }
    if ((y & 31U) == 31U) delay(1);
  }
  Serial.printf("[X4LAB] prepared fixed 800x480 diagnostic frames: pattern=%lu inverse=%lu white=%lu bytes\n",
                static_cast<unsigned long>(GHOST_FRAME_BYTES), static_cast<unsigned long>(GHOST_FRAME_BYTES),
                static_cast<unsigned long>(GHOST_FRAME_BYTES));
  return true;
}

bool stream_controller_frame(uint8_t command, const uint8_t* visible) {
  if (!visible || !write_command(command)) return false;
  gpio_set_level(PIN_EPD_DC, 1);
  gpio_set_level(PIN_EPD_CS, 0);
  uint16_t controller_row = 0;
  while (controller_row < CONTROLLER_HEIGHT) {
    check_abort();
    poll_mode_serial_abort();
    const uint16_t rows = std::min<uint16_t>(SPI_ROWS_PER_CHUNK, CONTROLLER_HEIGHT - controller_row);
    for (uint16_t local = 0; local < rows; ++local) {
      const uint16_t row = static_cast<uint16_t>(controller_row + local);
      uint8_t* destination = dma_buffer + static_cast<size_t>(local) * ROW_BYTES;
      if (row < GATE_OFFSET || row >= GATE_OFFSET + PANEL_HEIGHT) {
        memset(destination, 0xFF, ROW_BYTES);
      } else {
        memcpy(destination, visible + static_cast<size_t>(row - GATE_OFFSET) * ROW_BYTES, ROW_BYTES);
      }
    }
    if (!spi_transmit(dma_buffer, static_cast<size_t>(rows) * ROW_BYTES)) {
      gpio_set_level(PIN_EPD_CS, 1);
      return false;
    }
    controller_row = static_cast<uint16_t>(controller_row + rows);
  }
  gpio_set_level(PIN_EPD_CS, 1);
  return true;
}

bool stream_visible_frame(uint8_t command, const uint8_t* visible) {
  if (!visible || !set_partial_window(0, PANEL_HEIGHT) ||
      !stream_cached_plane(command, visible, GHOST_FRAME_BYTES)) {
    return false;
  }
  return write_command(CMD_PARTIAL_OUT);
}

void build_corrected_absolute_lut(uint8_t out[5][LUT_BYTES], uint8_t frames) {
  memset(out, 0, 5U * LUT_BYTES);
  for (size_t table = 0; table < 5; ++table) {
    out[table][0] = 0x01;
    out[table][5] = 0x01;
    out[table][6] = 0x01;
  }
  out[0][1] = frames;
  // Product-driver 0.1.2 correction: register 0x21..0x24 = 81,81,41,41
  // for a one-frame pulse. Both NEW=white buckets whiten; both NEW=black
  // buckets blacken, independent of stale DTM1.
  out[1][1] = static_cast<uint8_t>(0x80U | frames);
  out[2][1] = static_cast<uint8_t>(0x80U | frames);
  out[3][1] = static_cast<uint8_t>(0x40U | frames);
  out[4][1] = static_cast<uint8_t>(0x40U | frames);
}

bool upload_corrected_absolute_lut(uint8_t frames) {
  uint8_t lut[5][LUT_BYTES]{};
  build_corrected_absolute_lut(lut, frames);
  for (size_t table = 0; table < 5; ++table) {
    if (!write_register(LUT_REGS[table], lut[table], LUT_BYTES)) return false;
  }
  return true;
}

bool ghost_power_on(uint32_t* duration_us) {
  if (duration_us) *duration_us = 0;
  if (screen_powered) return true;
  if (!write_command(CMD_POWER_ON)) return false;
  uint32_t measured = 0;
  if (!wait_busy_cycle(POWER_TIMEOUT_MS, "power on", &measured)) return false;
  screen_powered = true;
  if (duration_us) *duration_us = measured;
  Serial.printf("[X4LAB] PON complete busy_cycle=%lu us\n", static_cast<unsigned long>(measured));
  return true;
}

bool ghost_power_off(uint32_t* duration_us) {
  if (duration_us) *duration_us = 0;
  if (!screen_powered) return true;
  if (!write_command(GHOST_CMD_POWER_OFF)) return false;
  uint32_t measured = 0;
  if (!wait_busy_cycle(POWER_TIMEOUT_MS, "power off", &measured)) return false;
  screen_powered = false;
  if (duration_us) *duration_us = measured;
  Serial.printf("[X4LAB] POF complete busy_cycle=%lu us\n", static_cast<unsigned long>(measured));
  return true;
}

bool configure_absolute_path(uint8_t frames) {
  const uint8_t psr[] = {PSR_EXTERNAL, PSR_1};
  uint32_t pon_us = 0;
  return write_register1(CMD_PLL, PLL_FAST) && set_partial_window(0, PANEL_HEIGHT) &&
         write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) && write_register1(CMD_PFS, PFS) &&
         write_register1(CMD_GATE_SCAN, GATE_SCAN) && write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FAST) &&
         write_register1(CMD_CCSET, CCSET) && write_register1(CMD_TSSET, TSSET_FAST) &&
         upload_corrected_absolute_lut(frames) && ghost_power_on(&pon_us);
}

bool trigger_refresh_and_close(const char* label, uint32_t* waveform_us) {
  uint32_t measured = 0;
  if (!write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, &measured) ||
      !write_command(CMD_PARTIAL_OUT)) {
    return false;
  }
  if (waveform_us) *waveform_us = measured;
  Serial.printf("[X4LAB] DRF label=%s busy_cycle=%lu us\n", label,
                static_cast<unsigned long>(measured));
  return true;
}

bool absolute_target_refresh(const uint8_t* target, uint8_t frames, const char* label) {
  const int64_t upload_start = esp_timer_get_time();
  if (!stream_visible_frame(CMD_DTM2, target)) return false;
  const uint32_t upload_us = static_cast<uint32_t>(esp_timer_get_time() - upload_start);
  if (!configure_absolute_path(frames)) return false;
  uint32_t waveform_us = 0;
  if (!trigger_refresh_and_close(label, &waveform_us)) return false;
  Serial.printf("[X4LAB] TARGET label=%s frames=%u upload=%lu us wave=%lu us\n", label,
                static_cast<unsigned>(frames), static_cast<unsigned long>(upload_us),
                static_cast<unsigned long>(waveform_us));
  return true;
}

bool resident_pulse(const char* label, uint32_t* waveform_us) {
  if (!screen_powered || !set_partial_window(0, PANEL_HEIGHT)) return false;
  return trigger_refresh_and_close(label, waveform_us);
}

bool otp_refresh_and_sync(const uint8_t* target, const char* label) {
  Serial.printf("[X4LAB] OTP baseline begin label=%s\n", label);
  if (!write_register1(CMD_PLL, PLL_STOCK) || !stream_controller_frame(CMD_DTM1, ghost_white) ||
      !stream_controller_frame(CMD_DTM2, target) || !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FULL) ||
      !write_register1(CMD_CCSET, CCSET) || !write_register1(CMD_TSSET, TSSET_FULL) ||
      !write_register1(CMD_PFS, PFS) || !write_register1(CMD_GATE_SCAN, GATE_SCAN)) {
    return false;
  }
  uint32_t pon_us = 0;
  if (!ghost_power_on(&pon_us)) return false;
  const uint8_t psr[] = {PSR_OTP, PSR_1};
  if (!write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) || !write_command(CMD_DISPLAY_REFRESH)) return false;
  uint32_t waveform_us = 0;
  if (!wait_refresh(REFRESH_TIMEOUT_MS, &waveform_us) || !stream_controller_frame(CMD_DTM1, target)) return false;
  Serial.printf("[X4LAB] OTP baseline complete label=%s pon=%lu us wave=%lu us dtm1=target\n", label,
                static_cast<unsigned long>(pon_us), static_cast<unsigned long>(waveform_us));
  return true;
}

bool run_resident_settling(const char* label) {
  const int64_t started = esp_timer_get_time();
  const int64_t deadline = started + static_cast<int64_t>(GHOST_SETTLE_MS) * 1000;
  uint32_t pulses = 0;
  uint64_t total_wave_us = 0;
  Serial.printf("[X4LAB] SETTLE begin label=%s duration_ms=%lu\n", label,
                static_cast<unsigned long>(GHOST_SETTLE_MS));
  while (esp_timer_get_time() < deadline) {
    check_abort();
    poll_mode_serial_abort();
    uint32_t wave_us = 0;
    if (!resident_pulse(label, &wave_us)) return false;
    ++pulses;
    total_wave_us += wave_us;
    delay(1);
  }
  Serial.printf("[X4LAB] SETTLE complete label=%s pulses=%lu elapsed_ms=%lld avg_wave_us=%llu\n", label,
                static_cast<unsigned long>(pulses),
                static_cast<long long>((esp_timer_get_time() - started) / 1000),
                pulses ? total_wave_us / pulses : 0ULL);
  return true;
}

bool synchronize_dtm1_white() {
  const int64_t started = esp_timer_get_time();
  if (!stream_controller_frame(CMD_DTM1, ghost_white)) return false;
  Serial.printf("[X4LAB] DTM1 synchronized to full-controller white elapsed_us=%lld\n",
                static_cast<long long>(esp_timer_get_time() - started));
  return true;
}

bool prepare_pattern_then_clear() {
  spi_device = spi_device_nominal;
  if (!controller_init(PLL_STOCK)) return false;
  if (!otp_refresh_and_sync(ghost_pattern, "high-contrast-pattern")) return false;
  if (!absolute_target_refresh(ghost_pattern, 1, "pattern-anchor")) return false;
  if (!run_resident_settling("pattern-burn-in")) return false;
  if (!absolute_target_refresh(ghost_white, 1, "white-clear")) return false;
  if (!run_resident_settling("white-clear")) return false;
  Serial.println("[X4LAB] COMMON sequence complete: visible=white DTM2=white DTM1=pattern panel=ON");
  return true;
}
