  // Keep the clean reference at factory source/VCOM settings for every mode.
  if (!stream_white_plane(CMD_DTM1) || !visual_stream_full_plane(CMD_DTM2, frame0) ||
      !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FULL) || !write_register1(CMD_CCSET, CCSET) ||
      !write_register1(CMD_TSSET, TSSET_FULL) || !power_on()) return false;
  const uint8_t psr[] = {PSR_OTP, PSR_1};
  if (!write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) ||
      !write_command(CMD_DISPLAY_REFRESH)) return false;
  uint32_t waveform_us = 0;
  if (!wait_refresh(REFRESH_TIMEOUT_MS, &waveform_us) ||
      !visual_stream_full_plane(CMD_DTM1, frame0)) return false;
  visual_current_frame = 0;
  Serial.printf("[X4LAB] baseline=%lu us\n", static_cast<unsigned long>(waveform_us));

  // Every candidate starts from the same physical/plane state, then enters its
  // own documented power, VCOM and timing profile through a fresh POF/PON.
  if (!visual_power_off() || !visual_write_power_profile(mode) || !power_on()) return false;
  // Reassert non-power registers after PON; some controller revisions restore
  // MTP defaults during the power transition.
  return visual_write_power_profile(mode);
}

void visual_lut_common(uint8_t out[5][LUT_BYTES]) {
  memset(out, 0, 5U * LUT_BYTES);
  for (size_t i = 0; i < 5; ++i) {
    out[i][0] = 0x01;
    out[i][5] = 0x01;
    out[i][6] = 0x01;
  }
}

void visual_build_lut(const VisualMode &mode, uint8_t out[5][LUT_BYTES]) {
  visual_lut_common(out);
  // Controller transition map established on this X4 family:
  // register 0x21=11, 0x22=01, 0x23=10, 0x24=00.
  switch (mode.waveform) {
    case VisualWaveform::AbsoluteTwo:
      out[0][1] = 2;
      // Current product absolute mapping: registers 0x21/0x22 follow
      // NEW=black; registers 0x23/0x24 follow NEW=white.
      out[1][1] = LEVEL_LOW | 2;
      out[2][1] = LEVEL_LOW | 2;
      out[3][1] = LEVEL_HIGH | 2;
      out[4][1] = LEVEL_HIGH | 2;
      break;
    case VisualWaveform::DifferentialTwo:
      out[0][1] = 2;
      out[1][1] = 2; out[4][1] = 2;
      out[2][1] = LEVEL_LOW | 2;
      out[3][1] = LEVEL_HIGH | 2;
      break;
    case VisualWaveform::Complementary:
    case VisualWaveform::ComplementaryDouble: {
      const uint8_t n = mode.waveform == VisualWaveform::Complementary ? 1 : 2;
      for (size_t i : {size_t(0), size_t(1), size_t(4)}) {
        out[i][1] = LEVEL_HIGH | n;
        out[i][2] = LEVEL_LOW | n;
      }
      out[2][1] = LEVEL_LOW | n; out[2][2] = LEVEL_LOW | n;
      out[3][1] = LEVEL_HIGH | n; out[3][2] = LEVEL_HIGH | n;
      break;
    }
    case VisualWaveform::KickVssHold:
      // W->B maximum then half-field hold, followed by the mirrored B->W pair.
      for (size_t i : {size_t(0), size_t(1), size_t(4)}) {
        out[i][1] = LEVEL_HIGH | 1; out[i][2] = LEVEL_HIGH | 1;
        out[i][3] = LEVEL_LOW | 1;  out[i][4] = LEVEL_LOW | 1;
      }
      out[2][1] = LEVEL_LOW | 1; out[2][2] = LEVEL_GND | 1;
      out[2][3] = LEVEL_LOW | 1; out[2][4] = LEVEL_LOW | 1;
      out[3][1] = LEVEL_HIGH | 1; out[3][2] = LEVEL_HIGH | 1;
      out[3][3] = LEVEL_HIGH | 1; out[3][4] = LEVEL_GND | 1;
      break;
    case VisualWaveform::KickVdhrHold:
      for (size_t i : {size_t(0), size_t(1), size_t(4)}) {
        out[i][1] = LEVEL_HIGH | 1; out[i][2] = LEVEL_HIGH | 1;
        out[i][3] = LEVEL_LOW | 1;  out[i][4] = LEVEL_LOW | 1;
      }
      out[2][1] = LEVEL_LOW | 1; out[2][2] = LEVEL_VDHR | 1;
      out[2][3] = LEVEL_LOW | 1; out[2][4] = LEVEL_LOW | 1;
      out[3][1] = LEVEL_HIGH | 1; out[3][2] = LEVEL_HIGH | 1;
      out[3][3] = LEVEL_HIGH | 1; out[3][4] = LEVEL_VDHR | 1;
      break;
    case VisualWaveform::ReverseActivation:
      // A one-frame, reduced reverse field precedes each maximum forward kick.
      for (size_t i : {size_t(0), size_t(1), size_t(4)}) {
        out[i][1] = LEVEL_LOW | 1; out[i][2] = LEVEL_HIGH | 1;
        out[i][3] = LEVEL_HIGH | 1; out[i][4] = LEVEL_LOW | 1;
      }
      out[2][1] = LEVEL_GND | 1; out[2][2] = LEVEL_LOW | 1;
      out[2][3] = LEVEL_HIGH | 1; out[2][4] = LEVEL_LOW | 1;
      out[3][1] = LEVEL_LOW | 1; out[3][2] = LEVEL_HIGH | 1;
      out[3][3] = LEVEL_GND | 1; out[3][4] = LEVEL_HIGH | 1;
      break;
  }
}

bool visual_upload_lut(const VisualMode &mode) {
  uint8_t lut[5][LUT_BYTES]{};
  visual_build_lut(mode, lut);
  for (size_t i = 0; i < 5; ++i)
