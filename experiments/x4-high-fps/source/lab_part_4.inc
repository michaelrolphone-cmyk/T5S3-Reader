  return write_command(CMD_PARTIAL_IN) && write_register(CMD_PARTIAL_WINDOW, window, sizeof(window));
}

void build_a2_lut(uint8_t out[5][LUT_BYTES], uint8_t drive_frames, uint8_t counter_frames) {
  memset(out, 0, 5 * LUT_BYTES);
  const uint8_t total = static_cast<uint8_t>(drive_frames + counter_frames);
  for (size_t table = 0; table < 5; ++table) {
    out[table][0] = 0x01;
    out[table][5] = 0x01;
    out[table][6] = 0x01;
  }
  // VCOM, unchanged-white, and unchanged-black remain at VSS for the same frame count.
  out[0][1] = total;
  out[1][1] = total;
  out[4][1] = total;
  // BW drives toward black (VSL), WB toward white (VSH). Optional second phase
  // remains available for controlled experiments, but the failed 4+1/4+2
  // counterpulse modes are not present in the v0.1.4 matrix.
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
  build_a2_lut(generated, mode.drive_frames, mode.counter_frames);
  for (size_t table = 0; table < 5; ++table) {
    if (!write_register(LUT_REGS[table], generated[table], LUT_BYTES)) return false;
  }
  return true;
}

bool begin_refresh(const TestMode& mode, uint32_t* waveform_us) {
  if (!write_register1(CMD_PLL, mode.pll)) return false;

  if (mode.waveform == Waveform::OtpDu) {
    // Byte-order parity with the recovered Xteink UC8279 DU trigger: CDI,
    // CCSET, TSSET, PFS, gate scan, PON, PTIN/PTL, OTP PSR, then DRF.
    if (!write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FAST) || !write_register1(CMD_CCSET, CCSET) ||
        !write_register1(CMD_TSSET, TSSET_FAST) || !write_register1(CMD_PFS, PFS) ||
        !write_register1(CMD_GATE_SCAN, GATE_SCAN) || !power_on() ||
        !set_partial_window(mode.window_y, mode.window_h)) {
      return false;
    }
    const uint8_t psr[] = {PSR_OTP, PSR_1};
    if (!write_register(CMD_PANEL_SETTING, psr, sizeof(psr))) return false;
  } else {
    // Byte-order parity with UC8279_aa_prebw_mid: PTIN/PTL, external PSR,
    // PFS, gate scan, CDI, CCSET, TSSET, LUT upload, PON, then DRF.
    const uint8_t psr[] = {PSR_EXTERNAL, PSR_1};
    if (!set_partial_window(mode.window_y, mode.window_h) ||
        !write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) || !write_register1(CMD_PFS, PFS) ||
        !write_register1(CMD_GATE_SCAN, GATE_SCAN) || !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FAST) ||
        !write_register1(CMD_CCSET, CCSET) || !write_register1(CMD_TSSET, TSSET_FAST) ||
        !upload_lut(mode) || !power_on()) {
      return false;
    }
  }

  if (!write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, waveform_us)) return false;
  return write_command(CMD_PARTIAL_OUT);
}

bool clean_baseline(uint16_t active_y, uint16_t active_h, Pattern pattern) {
  Serial.println("[X4LAB] establishing OTP full-clean baseline");
  if (!write_register1(CMD_PLL, PLL_STOCK) || !stream_white_plane(CMD_DTM1) ||
      !stream_plane(CMD_DTM2, 0, active_y, active_h, pattern) ||
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
  if (!data || !set_partial_window(mode.window_y, mode.window_h) ||
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
  Serial.printf("[X4LAB] transport=%s rows_sent_per_plane=%u\n",
                mode.transport == Transport::CachedWindow ? "cached-window" : "legacy-full-stride",
                static_cast<unsigned>(mode.transport == Transport::CachedWindow ? mode.window_h : CONTROLLER_HEIGHT));

  if (!prepare_frame_cache(mode)) return false;
  if (!clean_baseline(mode.window_y, mode.window_h, mode.pattern)) {
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

