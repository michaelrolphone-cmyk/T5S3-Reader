    if (!write_register(LUT_REGS[i], lut[i], LUT_BYTES)) return false;
  return true;
}

bool visual_begin_refresh(const VisualMode &mode, uint32_t *wave_us) {
  const uint8_t psr[] = {PSR_EXTERNAL, PSR_1};
  if (!set_partial_window(VISUAL_Y, VISUAL_H) ||
      !write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) ||
      !write_register1(CMD_PFS, PFS) || !write_register1(CMD_GATE_SCAN, GATE_SCAN) ||
      !write_register1(CMD_VCOM_DATA_INTERVAL, mode.cdi) ||
      !write_register1(CMD_CCSET, CCSET) || !write_register1(CMD_TSSET, TSSET_FAST) ||
      !write_register1(V_CMD_TCON, mode.tcon) || !write_register1(CMD_PLL, mode.pll) ||
      (mode.vcom != VCOM_FACTORY && !write_register1(V_CMD_VCOM_DC, mode.vcom)) ||
      !visual_upload_lut(mode) || !power_on()) return false;
  if (!write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, wave_us)) return false;
  return write_command(CMD_PARTIAL_OUT);
}

bool visual_uses_old(const VisualMode &mode) {
  return mode.waveform != VisualWaveform::AbsoluteTwo;
}

bool visual_run_mode(size_t index) {
  if (index >= VISUAL_MODE_COUNT) return false;
  const VisualMode &mode = VISUAL_MODES[index];
  spi_device = spi_device_nominal;
  soft_stop_requested = false;
  while (gpio_get_level(PIN_BTN_POWER) == 0) { check_abort(); delay(10); }

  Serial.printf("\n[X4LAB] MODE %u/%u: %s\n", static_cast<unsigned>(index + 1),
                static_cast<unsigned>(VISUAL_MODE_COUNT), mode.name);
  Serial.println("[X4LAB] side button=IMMEDIATE RESET/HALT; POWER=stop after current refresh");
  Serial.printf("[X4LAB] window=800x%u y=%u pattern=%u pll=%uHz VDH=%umV VDL=-%umV VDHR=%umV ",
                VISUAL_H, VISUAL_Y, static_cast<unsigned>(mode.pattern), pll_hz(mode.pll),
                rail_mv(mode.vdh), rail_mv(mode.vdl), rail_mv(mode.vdhr));
  if (mode.vcom == VCOM_FACTORY) Serial.print("VCOM=factory ");
  else Serial.printf("VCOM=%dmV ", vcom_mv(mode.vcom));
  Serial.printf("TCON=%02x CDI=%02x\n", mode.tcon, mode.cdi);

  if (!visual_prepare_cache(mode) || !visual_clean_baseline(mode)) {
    visual_release_cache();
    return false;
  }

  uint64_t sum_upload = 0, sum_wave = 0, sum_sync = 0, sum_total = 0;
  uint16_t completed = 0;
  for (uint16_t n = 0; n < mode.frames; ++n) {
    check_abort();
    if (gpio_get_level(PIN_BTN_POWER) == 0) soft_stop_requested = true;
    if (soft_stop_requested) break;
    const uint32_t next = visual_current_frame + 1U;
    const int64_t start = esp_timer_get_time();
    const int64_t upload_start = start;
    if (!visual_stream_window_plane(CMD_DTM2, next)) { visual_release_cache(); return false; }
    const uint32_t upload_us = static_cast<uint32_t>(esp_timer_get_time() - upload_start);
    uint32_t wave_us = 0;
    if (!visual_begin_refresh(mode, &wave_us)) { visual_release_cache(); return false; }
    uint32_t sync_us = 0;
    if (visual_uses_old(mode)) {
      const int64_t sync_start = esp_timer_get_time();
      if (!visual_stream_window_plane(CMD_DTM1, next)) { visual_release_cache(); return false; }
      sync_us = static_cast<uint32_t>(esp_timer_get_time() - sync_start);
    }
    const uint32_t total_us = static_cast<uint32_t>(esp_timer_get_time() - start);
    visual_current_frame = next;
    sum_upload += upload_us; sum_wave += wave_us; sum_sync += sync_us; sum_total += total_us;
    ++completed;
    const uint32_t fps100 = total_us ? static_cast<uint32_t>(100000000ULL / total_us) : 0;
    Serial.printf("[X4LAB] frame=%u upload=%lu wave=%lu sync=%lu total=%lu fps=%lu.%02lu\n",
                  static_cast<unsigned>(n + 1), static_cast<unsigned long>(upload_us),
                  static_cast<unsigned long>(wave_us), static_cast<unsigned long>(sync_us),
                  static_cast<unsigned long>(total_us), static_cast<unsigned long>(fps100 / 100),
                  static_cast<unsigned long>(fps100 % 100));
    delay(1);
  }
  if (completed) {
    const uint64_t avg = sum_total / completed;
    const uint64_t fps100 = avg ? 100000000ULL / avg : 0;
    Serial.printf("[X4LAB] RESULT mode=%u frames=%u avg_upload=%llu avg_wave=%llu avg_sync=%llu avg_total=%llu fps=%llu.%02llu\n",
                  static_cast<unsigned>(index + 1), static_cast<unsigned>(completed),
                  sum_upload / completed, sum_wave / completed, sum_sync / completed, avg,
                  fps100 / 100, fps100 % 100);
  }
  Serial.println("[X4LAB] inspect retained final image; next mode begins with a common OTP baseline");
  visual_release_cache();
  while (gpio_get_level(PIN_BTN_POWER) == 0) delay(10);
  return true;
}

char visual_key(size_t index) {
  if (index < 9) return static_cast<char>('1' + index);
  return static_cast<char>('a' + index - 9);
}
int visual_key_index(int c) {
  if (c >= '1' && c <= '9') return c - '1';
  if (c >= 'a' && c <= 'p') return 9 + c - 'a';
  if (c >= 'A' && c <= 'P') return 9 + c - 'A';
  return -1;
}

