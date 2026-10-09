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

bool ghost_stream_visible_plane(uint8_t command, const uint8_t* visible) {
  if (!visible || !set_partial_window(0, PANEL_HEIGHT) ||
      !stream_cached_plane(command, visible, GHOST_VISIBLE_BYTES)) {
    return false;
  }
  return write_command(CMD_PARTIAL_OUT);
}

bool ghost_sync_dtm1(const uint8_t* visible, const char* state) {
  const int64_t started = esp_timer_get_time();
  if (!ghost_stream_full_controller_plane(CMD_DTM1, visible)) return false;
  ghost_dtm1_state = state;
  Serial.printf("[X4LAB] DTM1 synchronized state=%s bytes=%lu elapsed=%lu us\n", state,
                static_cast<unsigned long>(CONTROLLER_HEIGHT * ROW_BYTES),
                static_cast<unsigned long>(esp_timer_get_time() - started));
  return true;
}

void ghost_build_absolute_lut(uint8_t out[5][LUT_BYTES], uint8_t frames) {
  memset(out, 0, 5U * LUT_BYTES);
  for (size_t table = 0; table < 5; ++table) {
    out[table][0] = 0x01;
    out[table][5] = 0x01;
    out[table][6] = 0x01;
  }
  out[0][1] = frames;
  out[1][1] = static_cast<uint8_t>(GHOST_ABSOLUTE_BLACK_RAIL | frames);
  out[2][1] = static_cast<uint8_t>(GHOST_ABSOLUTE_BLACK_RAIL | frames);
  out[3][1] = static_cast<uint8_t>(GHOST_ABSOLUTE_WHITE_RAIL | frames);
  out[4][1] = static_cast<uint8_t>(GHOST_ABSOLUTE_WHITE_RAIL | frames);
}

bool ghost_upload_absolute_lut(uint8_t frames) {
  uint8_t lut[5][LUT_BYTES]{};
  ghost_build_absolute_lut(lut, frames);
  for (size_t table = 0; table < 5; ++table) {
    if (!write_register(LUT_REGS[table], lut[table], LUT_BYTES)) return false;
  }
  return true;
}

void ghost_build_differential_lut(uint8_t out[5][LUT_BYTES], uint8_t frames) {
  memset(out, 0, 5U * LUT_BYTES);
  for (size_t table = 0; table < 5; ++table) {
    out[table][0] = 0x01;
    out[table][5] = 0x01;
    out[table][6] = 0x01;
  }
  out[0][1] = frames;
  out[1][1] = frames;  // white->white: VSS / no optical drive
  out[2][1] = static_cast<uint8_t>(GHOST_ABSOLUTE_BLACK_RAIL | frames);  // white->black
  out[3][1] = static_cast<uint8_t>(GHOST_ABSOLUTE_WHITE_RAIL | frames);  // black->white
  out[4][1] = frames;  // black->black: VSS / no optical drive
}

bool ghost_upload_differential_lut(uint8_t frames) {
  uint8_t lut[5][LUT_BYTES]{};
  ghost_build_differential_lut(lut, frames);
  for (size_t table = 0; table < 5; ++table) {
    if (!write_register(LUT_REGS[table], lut[table], LUT_BYTES)) return false;
  }
  return true;
}

bool ghost_prepare_fast_profile(uint8_t frames) {
  const uint8_t psr[] = {PSR_EXTERNAL, PSR_1};
  if (!write_register1(CMD_PLL, PLL_FAST) || !set_partial_window(0, PANEL_HEIGHT) ||
      !write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) || !write_register1(CMD_PFS, PFS) ||
      !write_register1(CMD_GATE_SCAN, GATE_SCAN) ||
      !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FAST) || !write_register1(CMD_CCSET, CCSET) ||
      !write_register1(CMD_TSSET, TSSET_FAST) || !ghost_upload_absolute_lut(frames) ||
      !ghost_power_on_timed("fast profile")) {
    return false;
  }
  // PON may reload MTP defaults on this family.  Reassert the external PSR
  // after a real power-on while preserving the already uploaded LUT bytes.
  return write_register(CMD_PANEL_SETTING, psr, sizeof(psr));
}

bool ghost_refresh_frame(const uint8_t* frame, const char* state, uint8_t drive_frames,
                         const char* stage) {
  const int64_t started = esp_timer_get_time();
  const int64_t upload_started = started;
  if (!ghost_stream_visible_plane(CMD_DTM2, frame)) return false;
  const uint32_t upload_us = static_cast<uint32_t>(esp_timer_get_time() - upload_started);
  ghost_dtm2_state = state;
  if (!ghost_prepare_fast_profile(drive_frames)) return false;
  uint32_t waveform_us = 0;
  if (!write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, &waveform_us) ||
      !write_command(CMD_PARTIAL_OUT)) {
    return false;
  }
  Serial.printf("[X4LAB] DRF stage=%s DTM1=%s DTM2=%s frames=%u upload=%lu us wave=%lu us total=%lu us\n",
                stage, ghost_dtm1_state, ghost_dtm2_state, static_cast<unsigned>(drive_frames),
                static_cast<unsigned long>(upload_us), static_cast<unsigned long>(waveform_us),
                static_cast<unsigned long>(esp_timer_get_time() - started));
  return true;
}

