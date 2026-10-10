  const int64_t upload_start = esp_timer_get_time();
  if (!focused_stream_rect_plane(CMD_DTM2, next, x, width)) return false;
  upload_sum += static_cast<uint32_t>(esp_timer_get_time() - upload_start);
  uint32_t wave_us = 0;
  if (!focused_refresh_rect(mode, FocusPass::Combined, x, width, &wave_us)) return false;
  wave_sum += wave_us;
  ++physical_refreshes;
  const int64_t sync_start = esp_timer_get_time();
  if (!focused_stream_rect_plane(CMD_DTM1, next, x, width)) return false;
  sync_sum += static_cast<uint32_t>(esp_timer_get_time() - sync_start);
  return true;
}

bool focused_run_logical_frame(const FocusMode &mode, uint32_t next,
                               uint64_t &upload_sum, uint64_t &wave_sum, uint64_t &sync_sum,
                               unsigned &physical_refreshes) {
  const FocusRect rect = focused_dirty_rect(mode, focused_current_frame, next);
  if (mode.execution == FocusExecution::SplitDirections) {
    const int64_t upload_start = esp_timer_get_time();
    if (!focused_stream_rect_plane(CMD_DTM2, next, rect.x, rect.width)) return false;
    upload_sum += static_cast<uint32_t>(esp_timer_get_time() - upload_start);
    uint32_t wave_us = 0;
    if (!focused_refresh_rect(mode, FocusPass::WhiteToBlackOnly, rect.x, rect.width, &wave_us)) return false;
    wave_sum += wave_us; ++physical_refreshes;
    if (mode.gap_ms) delay(mode.gap_ms);
    wave_us = 0;
    if (!focused_refresh_rect(mode, FocusPass::BlackToWhiteOnly, rect.x, rect.width, &wave_us)) return false;
    wave_sum += wave_us; ++physical_refreshes;
    const int64_t sync_start = esp_timer_get_time();
    if (!focused_stream_rect_plane(CMD_DTM1, next, rect.x, rect.width)) return false;
    sync_sum += static_cast<uint32_t>(esp_timer_get_time() - sync_start);
    return true;
  }

  const uint8_t segments = mode.execution == FocusExecution::Segmented ? mode.segments : 1U;
  if (segments == 0 || rect.width % segments != 0) return false;
  const uint16_t segment_width = static_cast<uint16_t>(rect.width / segments);
  if ((segment_width & 7U) != 0) return false;
  for (uint8_t segment = 0; segment < segments; ++segment) {
    const uint16_t x = static_cast<uint16_t>(rect.x + static_cast<uint16_t>(segment) * segment_width);
    if (!focused_run_combined_segment(mode, next, x, segment_width,
                                      upload_sum, wave_sum, sync_sum, physical_refreshes)) return false;
    if (mode.gap_ms && segment + 1U < segments) delay(mode.gap_ms);
  }
  return true;
}

bool focused_run_mode(size_t index) {
  if (index >= FOCUSED_MODE_COUNT) return false;
  const FocusMode &mode = FOCUSED_MODES[index];
  spi_device = spi_device_nominal;
  soft_stop_requested = false;
  while (gpio_get_level(PIN_BTN_POWER) == 0) { check_abort(); delay(10); }

  Serial.printf("\n[X4LAB] MODE %u/%u: %s\n", static_cast<unsigned>(index + 1),
                static_cast<unsigned>(FOCUSED_MODE_COUNT), mode.name);
  Serial.println("[X4LAB] side button=IMMEDIATE RESET/HALT; POWER=stop after current logical frame");
  Serial.printf("[X4LAB] black=%u white=%u hold=%u VDH=%umV VDL=-%umV TCON=%02x CDI=%02x width=%u segments=%u gap=%ums execution=%u\n",
                mode.black_frames, mode.white_frames, mode.hold_frames,
                focused_rail_mv(mode.vdh), focused_rail_mv(mode.vdl), mode.tcon, mode.cdi,
                mode.transition_width, mode.segments, mode.gap_ms,
                static_cast<unsigned>(mode.execution));

  if (!focused_prepare_cache(mode) || !focused_clean_baseline(mode)) {
    focused_release_cache();
    return false;
  }

  uint64_t total_upload = 0, total_wave = 0, total_sync = 0, total_elapsed = 0;
  uint32_t total_refreshes = 0;
  uint16_t completed = 0;
  for (uint16_t n = 0; n < mode.logical_frames; ++n) {
    check_abort();
    if (gpio_get_level(PIN_BTN_POWER) == 0) soft_stop_requested = true;
    if (soft_stop_requested) break;
    const uint32_t next = focused_current_frame + 1U;
    const int64_t started = esp_timer_get_time();
    uint64_t upload = 0, wave = 0, sync = 0;
    unsigned refreshes = 0;
    if (!focused_run_logical_frame(mode, next, upload, wave, sync, refreshes)) {
      focused_release_cache();
      return false;
    }
    const uint32_t elapsed = static_cast<uint32_t>(esp_timer_get_time() - started);
    focused_current_frame = next;
    total_upload += upload; total_wave += wave; total_sync += sync; total_elapsed += elapsed;
    total_refreshes += refreshes;
    ++completed;
    const FocusRect rect = focused_dirty_rect(mode, next - 1U, next);
    const uint32_t fps100 = elapsed ? static_cast<uint32_t>(100000000ULL / elapsed) : 0;
    Serial.printf("[X4LAB] frame=%u rect=%u+%u refreshes=%u upload=%llu wave=%llu sync=%llu total=%lu fps=%lu.%02lu\n",
                  static_cast<unsigned>(n + 1), rect.x, rect.width, refreshes,
                  upload, wave, sync, static_cast<unsigned long>(elapsed),
                  static_cast<unsigned long>(fps100 / 100), static_cast<unsigned long>(fps100 % 100));
    delay(1);
  }
  if (completed) {
    const uint64_t avg = total_elapsed / completed;
    const uint64_t fps100 = avg ? 100000000ULL / avg : 0;
    Serial.printf("[X4LAB] RESULT mode=%u frames=%u physical_refreshes=%lu avg_upload=%llu avg_wave=%llu avg_sync=%llu avg_total=%llu fps=%llu.%02llu\n",
                  static_cast<unsigned>(index + 1), static_cast<unsigned>(completed),
                  static_cast<unsigned long>(total_refreshes), total_upload / completed,
                  total_wave / completed, total_sync / completed, avg, fps100 / 100, fps100 % 100);
  }
  Serial.println("[X4LAB] inspect the retained target and both old locations before starting another mode");
  focused_release_cache();
  while (gpio_get_level(PIN_BTN_POWER) == 0) delay(10);
  return true;
}

