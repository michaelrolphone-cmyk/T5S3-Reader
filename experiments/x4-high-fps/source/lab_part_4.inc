  return write_command(CMD_PARTIAL_IN) && write_register(CMD_PARTIAL_WINDOW, window, sizeof(window));
}

bool apply_scan_geometry(uint16_t gate_start_value, uint16_t gate_count, uint8_t tcon) {
  if (gate_count == 0 || gate_start_value + gate_count > CONTROLLER_HEIGHT) return false;
  const uint8_t resolution[] = {
      static_cast<uint8_t>(PANEL_WIDTH >> 8), static_cast<uint8_t>(PANEL_WIDTH),
      static_cast<uint8_t>(gate_count >> 8), static_cast<uint8_t>(gate_count)};
  const uint8_t gate_start[] = {
      0x00, 0x00, static_cast<uint8_t>((gate_start_value >> 8) & 0x03U),
      static_cast<uint8_t>(gate_start_value)};
  if (!wait_busy_high(REFRESH_TIMEOUT_MS, "scan geometry")) return false;
  return write_register1(CMD_TCON, tcon) &&
         write_register(CMD_RESOLUTION, resolution, sizeof(resolution)) &&
         write_register(CMD_GATE_SOURCE_START, gate_start, sizeof(gate_start));
}

bool restore_full_geometry() {
  return apply_scan_geometry(0, CONTROLLER_HEIGHT, TCON_DEFAULT);
}

bool configure_mode_geometry(const TestMode& mode) {
  if (!mode.compact_geometry) return restore_full_geometry();
  const uint16_t physical_gate_start = static_cast<uint16_t>(GATE_OFFSET + mode.window_y);
  return apply_scan_geometry(physical_gate_start, mode.window_h, mode.tcon);
}

spi_device_handle_t select_spi_device(uint32_t hz) {
  if (hz == SPI_TURBO_HZ) return spi_device_turbo;
  if (hz == SPI_OVERCLOCK_HZ) return spi_device_overclock;
  if (hz == SPI_CLOCK_HZ) return spi_device_nominal;
  return nullptr;
}

void build_a2_lut(uint8_t out[5][LUT_BYTES], uint8_t drive_frames, uint8_t counter_frames,
                  bool absolute_drive) {
  memset(out, 0, 5 * LUT_BYTES);
  const uint8_t total = static_cast<uint8_t>(drive_frames + counter_frames);
  for (size_t table = 0; table < 5; ++table) {
    out[table][0] = 0x01;
    out[table][5] = 0x01;
    out[table][6] = 0x01;
  }
  out[0][1] = total;

  if (absolute_drive) {
    // Both possible old states drive directly toward the requested new state.
    // DTM1 is irrelevant, so no post-refresh old-plane synchronization is needed.
    out[1][1] = static_cast<uint8_t>(0x40U | drive_frames);  // WW -> white
    out[2][1] = static_cast<uint8_t>(0x80U | drive_frames);  // BW -> black
    out[3][1] = static_cast<uint8_t>(0x40U | drive_frames);  // WB -> white
    out[4][1] = static_cast<uint8_t>(0x80U | drive_frames);  // BB -> black
    return;
  }

  out[1][1] = total;
  out[4][1] = total;
  out[2][1] = static_cast<uint8_t>(0x80U | drive_frames);
  out[3][1] = static_cast<uint8_t>(0x40U | drive_frames);
  if (counter_frames != 0) {
    out[2][2] = static_cast<uint8_t>(0x40U | counter_frames);
    out[3][2] = static_cast<uint8_t>(0x80U | counter_frames);
  }
}

bool upload_lut(const TestMode& mode) {
  if (mode.waveform == Waveform::OtpDu) return true;
  if (mode.waveform == Waveform::StockPreBw) {
    for (size_t table = 0; table < 5; ++table) {
      if (!write_register(LUT_REGS[table], STOCK_PREBW[table], LUT_BYTES)) return false;
    }
    return true;
  }
  uint8_t generated[5][LUT_BYTES]{};
  build_a2_lut(generated, mode.drive_frames, mode.counter_frames,
               mode.waveform == Waveform::GeneratedAbsoluteA2);
  for (size_t table = 0; table < 5; ++table) {
    if (!write_register(LUT_REGS[table], generated[table], LUT_BYTES)) return false;
  }
  return true;
}

bool begin_refresh(const TestMode& mode, uint32_t* waveform_us) {
  if (!write_register1(CMD_PLL, mode.pll)) return false;

  if (mode.waveform == Waveform::OtpDu) {
    if (!write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FAST) || !write_register1(CMD_CCSET, CCSET) ||
        !write_register1(CMD_TSSET, TSSET_FAST) || !write_register1(CMD_PFS, PFS) ||
        !write_register1(CMD_GATE_SCAN, GATE_SCAN) || !power_on() ||
        (!mode.compact_geometry && !set_partial_window(mode.window_y, mode.window_h))) {
      return false;
    }
    const uint8_t psr[] = {PSR_OTP, PSR_1};
    if (!write_register(CMD_PANEL_SETTING, psr, sizeof(psr))) return false;
  } else {
    const uint8_t psr[] = {PSR_EXTERNAL, PSR_1};
    if ((!mode.compact_geometry && !set_partial_window(mode.window_y, mode.window_h)) ||
        !write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) || !write_register1(CMD_PFS, PFS) ||
        !write_register1(CMD_GATE_SCAN, GATE_SCAN) || !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FAST) ||
        !write_register1(CMD_CCSET, CCSET) || !write_register1(CMD_TSSET, TSSET_FAST) ||
        !upload_lut(mode) || !power_on()) {
      return false;
    }
  }

  // A sub-millisecond BUSY cycle is a missed/false completion, not a physical
  // panel refresh. Retry the DRF command and never include it in FPS telemetry.
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    uint32_t measured_us = 0;
    if (!write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, &measured_us)) return false;
    if (measured_us >= MIN_VALID_REFRESH_US) {
      if (waveform_us) *waveform_us = measured_us;
      return mode.compact_geometry ? true : write_command(CMD_PARTIAL_OUT);
    }
    Serial.printf("[X4LAB] rejected false BUSY completion duration=%lu us attempt=%u\n",
                  static_cast<unsigned long>(measured_us), static_cast<unsigned>(attempt + 1));
    delay(1);
  }
  Serial.println("[X4LAB] display refresh failed BUSY validation three times");
  return false;
}

bool clean_baseline(uint16_t active_y, uint16_t active_h, Pattern pattern) {
  Serial.println("[X4LAB] establishing OTP full-clean baseline");
  spi_device = spi_device_nominal;
  if (!restore_full_geometry() || !write_register1(CMD_PLL, PLL_STOCK) ||
      !stream_white_plane(CMD_DTM1) || !stream_plane(CMD_DTM2, 0, active_y, active_h, pattern) ||
      !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FULL) || !write_register1(CMD_CCSET, CCSET) ||
      !write_register1(CMD_TSSET, TSSET_FULL) || !power_on()) {
    return false;
  }
  const uint8_t psr[] = {PSR_OTP, PSR_1};
  if (!write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) || !write_command(CMD_DISPLAY_REFRESH)) return false;
  uint32_t waveform_us = 0;
  if (!wait_refresh(REFRESH_TIMEOUT_MS, &waveform_us)) return false;
  if (!stream_plane(CMD_DTM1, 0, active_y, active_h, pattern)) return false;
  current_frame = 0;
  Serial.printf("[X4LAB] clean baseline waveform=%lu us\n", static_cast<unsigned long>(waveform_us));
  return true;
}

bool stream_mode_plane(const TestMode& mode, uint8_t command, uint32_t frame) {
  if (mode.transport == Transport::LegacyFullStride) {
    return stream_plane(command, frame, mode.window_y, mode.window_h, mode.pattern);
  }
  const uint8_t* data = cached_frame(frame);
  if (!data) return false;
  if (mode.compact_geometry) {
    return stream_cached_plane(command, data, frame_cache_stride);
  }
  if (!set_partial_window(mode.window_y, mode.window_h) ||
      !stream_cached_plane(command, data, frame_cache_stride)) {
    return false;
  }
  return write_command(CMD_PARTIAL_OUT);
}

bool run_mode(size_t index) {
  if (index >= MODE_COUNT) return false;
  const TestMode& mode = MODES[index];
  soft_stop_requested = false;
  while (gpio_get_level(PIN_BTN_POWER) == 0) {
    check_abort();
    delay(10);
  }
  Serial.printf("\n[X4LAB] MODE %u/%u: %s\n", static_cast<unsigned>(index + 1), static_cast<unsigned>(MODE_COUNT),
                mode.name);
  Serial.println("[X4LAB] EITHER SIDE BUTTON = IMMEDIATE HARD ABORT; power button = stop after current refresh");
  Serial.printf("[X4LAB] active=800x%u physical_y=%u geometry=%s tcon=0x%02x pll=0x%02x spi=%luMHz\n",
                static_cast<unsigned>(mode.window_h), static_cast<unsigned>(mode.window_y),
                mode.compact_geometry ? "compact-TRES/GSST" : "controller-600-partial",
                static_cast<unsigned>(mode.tcon), static_cast<unsigned>(mode.pll),
                static_cast<unsigned long>(mode.spi_hz / 1000000U));

  if (!prepare_frame_cache(mode)) return false;
  if (!clean_baseline(mode.window_y, mode.window_h, mode.pattern)) {
    release_frame_cache();
    return false;
  }
  if (!configure_mode_geometry(mode)) {
    release_frame_cache();
    return false;
  }
  spi_device = select_spi_device(mode.spi_hz);
  if (!spi_device) {
    release_frame_cache();
    return false;
  }

  uint64_t sum_total = 0;
  uint64_t sum_waveform = 0;
  uint64_t sum_upload = 0;
  uint64_t sum_sync = 0;
  uint16_t completed = 0;
  for (uint16_t n = 0; n < mode.frame_count; ++n) {
    check_abort();
    if (gpio_get_level(PIN_BTN_POWER) == 0) soft_stop_requested = true;
    if (soft_stop_requested) break;

