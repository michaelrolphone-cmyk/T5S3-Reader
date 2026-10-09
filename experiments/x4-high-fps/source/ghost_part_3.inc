bool ghost_refresh_differential(const uint8_t* frame, const char* state, uint8_t drive_frames,
                                const char* stage, bool synchronize_after) {
  const int64_t started = esp_timer_get_time();
  const int64_t upload_started = started;
  if (!ghost_stream_visible_plane(CMD_DTM2, frame)) return false;
  const uint32_t upload_us = static_cast<uint32_t>(esp_timer_get_time() - upload_started);
  ghost_dtm2_state = state;

  const uint8_t psr[] = {PSR_EXTERNAL, PSR_1};
  if (!write_register1(CMD_PLL, PLL_FAST) || !set_partial_window(0, PANEL_HEIGHT) ||
      !write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) || !write_register1(CMD_PFS, PFS) ||
      !write_register1(CMD_GATE_SCAN, GATE_SCAN) ||
      !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FAST) || !write_register1(CMD_CCSET, CCSET) ||
      !write_register1(CMD_TSSET, TSSET_FAST) || !ghost_upload_differential_lut(drive_frames) ||
      !ghost_power_on_timed("differential profile") ||
      !write_register(CMD_PANEL_SETTING, psr, sizeof(psr))) {
    return false;
  }

  uint32_t waveform_us = 0;
  if (!write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, &waveform_us) ||
      !write_command(CMD_PARTIAL_OUT)) {
    return false;
  }
  if (synchronize_after && !ghost_sync_dtm1(frame, state)) return false;
  Serial.printf("[X4LAB] DIFF_DRF stage=%s DTM1=%s DTM2=%s frames=%u upload=%lu us wave=%lu us total=%lu us\n",
                stage, ghost_dtm1_state, ghost_dtm2_state, static_cast<unsigned>(drive_frames),
                static_cast<unsigned long>(upload_us), static_cast<unsigned long>(waveform_us),
                static_cast<unsigned long>(esp_timer_get_time() - started));
  return true;
}

bool ghost_resident_refresh(const char* stage, uint32_t* waveform_us) {
  if (!screen_powered || !set_partial_window(0, PANEL_HEIGHT)) return false;
  uint32_t wave = 0;
  if (!write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, &wave) ||
      !write_command(CMD_PARTIAL_OUT)) {
    return false;
  }
  if (waveform_us) *waveform_us = wave;
  Serial.printf("[X4LAB] RESIDENT_DRF stage=%s DTM1=%s DTM2=%s wave=%lu us\n", stage,
                ghost_dtm1_state, ghost_dtm2_state, static_cast<unsigned long>(wave));
  return true;
}

bool ghost_settle(const char* label) {
  Serial.printf("[X4LAB] settle begin label=%s target_ms=%lu DTM1=%s DTM2=%s\n", label,
                static_cast<unsigned long>(GHOST_SETTLE_MS), ghost_dtm1_state, ghost_dtm2_state);
  const int64_t started = esp_timer_get_time();
  const int64_t deadline = started + static_cast<int64_t>(GHOST_SETTLE_MS) * 1000;
  uint64_t sum = 0;
  uint32_t minimum = UINT32_MAX;
  uint32_t maximum = 0;
  unsigned pulses = 0;
  while (esp_timer_get_time() < deadline) {
    if (ghost_stop_requested()) break;
    uint32_t wave = 0;
    if (!ghost_resident_refresh(label, &wave)) return false;
    sum += wave;
    minimum = std::min(minimum, wave);
    maximum = std::max(maximum, wave);
    ++pulses;
    delay(1);
  }
  const uint32_t elapsed_ms = static_cast<uint32_t>((esp_timer_get_time() - started) / 1000);
  Serial.printf("[X4LAB] settle end label=%s pulses=%u elapsed_ms=%lu avg_wave=%llu us min=%lu max=%lu\n",
                label, pulses, static_cast<unsigned long>(elapsed_ms),
                pulses ? static_cast<unsigned long long>(sum / pulses) : 0ULL,
                static_cast<unsigned long>(pulses ? minimum : 0), static_cast<unsigned long>(maximum));
  return true;
}

bool ghost_hold(const char* label, uint32_t duration_ms, bool maintenance_at_30s) {
  const int64_t started = esp_timer_get_time();
  int64_t next_log = started;
  bool maintenance_done = false;
  Serial.printf("[X4LAB] HOLD begin label=%s duration_ms=%lu powered=%u DTM1=%s DTM2=%s\n", label,
                static_cast<unsigned long>(duration_ms), screen_powered ? 1U : 0U,
                ghost_dtm1_state, ghost_dtm2_state);
  while (true) {
    if (ghost_stop_requested()) break;
    const int64_t now = esp_timer_get_time();
    const uint32_t elapsed_ms = static_cast<uint32_t>((now - started) / 1000);
    if (elapsed_ms >= duration_ms) break;
    if (maintenance_at_30s && !maintenance_done && elapsed_ms >= 30000U) {
      Serial.println("[X4LAB] MAINTENANCE cue=t+30s issuing one resident DRF now");
      uint32_t wave = 0;
      if (!ghost_resident_refresh("maintenance-t30", &wave)) return false;
      maintenance_done = true;
      Serial.printf("[X4LAB] MAINTENANCE complete wave=%lu us\n", static_cast<unsigned long>(wave));
    }
    if (now >= next_log) {
      Serial.printf("[X4LAB] OBSERVE label=%s t=%lu/%lu s powered=%u BUSY_N=%d DTM1=%s DTM2=%s\n",
                    label, static_cast<unsigned long>(elapsed_ms / 1000),
                    static_cast<unsigned long>(duration_ms / 1000), screen_powered ? 1U : 0U,
                    gpio_get_level(PIN_EPD_BUSY), ghost_dtm1_state, ghost_dtm2_state);
      next_log = now + static_cast<int64_t>(GHOST_LOG_INTERVAL_MS) * 1000;
    }
    delay(10);
  }
  Serial.printf("[X4LAB] HOLD end label=%s elapsed_ms=%lu powered=%u stopped=%u\n", label,
                static_cast<unsigned long>((esp_timer_get_time() - started) / 1000),
                screen_powered ? 1U : 0U, soft_stop_requested ? 1U : 0U);
  return true;
}

bool ghost_otp_clean_white() {
  Serial.println("[X4LAB] CLEAN reset + full-controller white DTM1/DTM2 + OTP refresh");
  if (!controller_init(PLL_STOCK)) return false;
  ghost_dtm1_state = "WHITE";
