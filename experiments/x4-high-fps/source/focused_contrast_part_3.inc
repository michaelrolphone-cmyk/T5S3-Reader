
bool focused_clean_baseline(const FocusMode &mode) {
  Serial.println("[X4LAB] establishing common factory OTP baseline");
  const uint8_t *frame0 = focused_frame(0);
  if (!frame0 || !controller_init(PLL_STOCK)) return false;
  if (!stream_white_plane(CMD_DTM1) || !focused_stream_full_plane(CMD_DTM2, frame0) ||
      !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FULL) || !write_register1(CMD_CCSET, CCSET) ||
      !write_register1(CMD_TSSET, TSSET_FULL) || !power_on()) return false;
  const uint8_t psr[] = {PSR_OTP, PSR_1};
  if (!write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) || !write_command(CMD_DISPLAY_REFRESH)) return false;
  uint32_t wave_us = 0;
  if (!wait_refresh(REFRESH_TIMEOUT_MS, &wave_us) || !focused_stream_full_plane(CMD_DTM1, frame0)) return false;
  focused_current_frame = 0;
  Serial.printf("[X4LAB] baseline=%lu us\n", static_cast<unsigned long>(wave_us));
  if (!focused_power_off() || !focused_write_power_profile(mode) || !power_on()) return false;
  return focused_write_power_profile(mode);
}

void focused_lut_common(uint8_t out[5][LUT_BYTES]) {
  memset(out, 0, 5U * LUT_BYTES);
  for (size_t i = 0; i < 5; ++i) {
    out[i][0] = 0x01;
    out[i][5] = 0x01;
    out[i][6] = 0x01;
  }
}

void focused_build_lut(const FocusMode &mode, FocusPass pass, uint8_t out[5][LUT_BYTES]) {
  focused_lut_common(out);
  // Transition map on this X4 family: 0x21=11, 0x22=01 (W->B),
  // 0x23=10 (B->W), 0x24=00. VCOM is table 0x20.
  if (pass == FocusPass::WhiteToBlackOnly) {
    const uint8_t n = mode.black_frames;
    out[0][1] = LEVEL_HIGH | n;
    out[1][1] = LEVEL_HIGH | n;
    out[2][1] = LEVEL_LOW | n;
    out[3][1] = LEVEL_HIGH | n;
    out[4][1] = LEVEL_HIGH | n;
    return;
  }
  if (pass == FocusPass::BlackToWhiteOnly) {
    const uint8_t n = mode.white_frames;
    out[0][1] = LEVEL_LOW | n;
    out[1][1] = LEVEL_LOW | n;
    out[2][1] = LEVEL_LOW | n;
    out[3][1] = LEVEL_HIGH | n;
    out[4][1] = LEVEL_LOW | n;
    return;
  }

  if (mode.waveform == FocusWaveform::SymmetricKickGndHold) {
    for (size_t i = 0; i < 5; ++i) {
      out[i][1] = LEVEL_HIGH | 1;
      out[i][2] = LEVEL_HIGH | 1;
      out[i][3] = LEVEL_LOW | 1;
      out[i][4] = LEVEL_LOW | 1;
    }
    out[2][1] = LEVEL_LOW | 1; out[2][2] = LEVEL_GND | 1;
    out[2][3] = LEVEL_LOW | 1; out[2][4] = LEVEL_LOW | 1;
    out[3][1] = LEVEL_HIGH | 1; out[3][2] = LEVEL_HIGH | 1;
    out[3][3] = LEVEL_HIGH | 1; out[3][4] = LEVEL_GND | 1;
    return;
  }

  const uint8_t black = mode.black_frames;
  const uint8_t white = mode.white_frames;
  for (size_t i : {size_t(0), size_t(1), size_t(4)}) {
    out[i][1] = LEVEL_HIGH | black;
    out[i][2] = LEVEL_LOW | white;
  }
  out[2][1] = LEVEL_LOW | black;
  out[2][2] = LEVEL_LOW | white;
  out[3][1] = LEVEL_HIGH | black;
  out[3][2] = LEVEL_HIGH | white;

  if (mode.waveform == FocusWaveform::WhiteGndHold && mode.hold_frames != 0) {
    const uint8_t hold = mode.hold_frames;
    out[0][3] = LEVEL_LOW | hold;
    out[1][3] = LEVEL_LOW | hold;
    out[2][3] = LEVEL_LOW | hold;
    out[3][3] = LEVEL_GND | hold;
    out[4][3] = LEVEL_LOW | hold;
  }
}

bool focused_upload_lut(const FocusMode &mode, FocusPass pass) {
  uint8_t lut[5][LUT_BYTES]{};
  focused_build_lut(mode, pass, lut);
  for (size_t i = 0; i < 5; ++i)
    if (!write_register(LUT_REGS[i], lut[i], LUT_BYTES)) return false;
  return true;
}

bool focused_refresh_rect(const FocusMode &mode, FocusPass pass, uint16_t x, uint16_t width,
                          uint32_t *wave_us) {
  const uint8_t psr[] = {PSR_EXTERNAL, PSR_1};
  if (!focused_set_partial_rect(x, width) ||
      !write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) ||
      !write_register1(CMD_PFS, PFS) || !write_register1(CMD_GATE_SCAN, GATE_SCAN) ||
      !write_register1(CMD_VCOM_DATA_INTERVAL, mode.cdi) ||
      !write_register1(CMD_CCSET, CCSET) || !write_register1(CMD_TSSET, TSSET_FAST) ||
      !write_register1(F_CMD_TCON, mode.tcon) || !write_register1(CMD_PLL, PLL_FAST) ||
      !focused_upload_lut(mode, pass) || !power_on()) return false;
  if (!write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, wave_us)) return false;
  return write_command(CMD_PARTIAL_OUT);
}

bool focused_run_combined_segment(const FocusMode &mode, uint32_t next, uint16_t x, uint16_t width,
                                  uint64_t &upload_sum, uint64_t &wave_sum, uint64_t &sync_sum,
                                  unsigned &physical_refreshes) {
