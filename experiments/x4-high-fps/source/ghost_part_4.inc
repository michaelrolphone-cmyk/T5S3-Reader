  ghost_dtm2_state = "WHITE";
  if (!stream_white_plane(CMD_DTM1) || !stream_white_plane(CMD_DTM2) ||
      !write_register1(CMD_VCOM_DATA_INTERVAL, CDI_FULL) || !write_register1(CMD_CCSET, CCSET) ||
      !write_register1(CMD_TSSET, TSSET_FULL) || !ghost_power_on_timed("OTP clean")) {
    return false;
  }
  const uint8_t psr[] = {PSR_OTP, PSR_1};
  uint32_t waveform_us = 0;
  if (!write_register(CMD_PANEL_SETTING, psr, sizeof(psr)) ||
      !write_command(CMD_DISPLAY_REFRESH) || !wait_refresh(REFRESH_TIMEOUT_MS, &waveform_us) ||
      !stream_white_plane(CMD_DTM1)) {
    return false;
  }
  Serial.printf("[X4LAB] CLEAN complete wave=%lu us powered=%u\n",
                static_cast<unsigned long>(waveform_us), screen_powered ? 1U : 0U);
  return true;
}

bool ghost_prepare_common_history() {
  if (!ghost_otp_clean_white()) return false;
  if (!ghost_refresh_frame(ghost_pattern, "PATTERN", 1, "burn-pattern")) return false;
  if (!ghost_settle("pattern-settle")) return false;
  if (soft_stop_requested) return true;
  if (!ghost_hold("pattern-burn-in", GHOST_PATTERN_HOLD_MS, false)) return false;
  if (soft_stop_requested) return true;

  // Make the high-contrast image the explicit OLD plane before clearing it.
  // The subsequent absolute white update intentionally leaves DTM1 stale.
  if (!ghost_sync_dtm1(ghost_pattern, "PATTERN")) return false;
  if (!ghost_refresh_frame(ghost_white, "WHITE", 1, "clear-white")) return false;
  if (!ghost_settle("white-settle")) return false;
  return true;
}

bool ghost_target_compensation(bool power_off_after) {
  // True transition-targeted bipolar sequence.  First make WHITE the truthful
  // OLD plane, then use a differential one-frame WHITE->PATTERN pulse so
  // unchanged white pixels remain at VSS.  Synchronize PATTERN as OLD, apply a
  // stronger two-frame PATTERN->WHITE differential restore, and synchronize
  // WHITE before the powered-idle or POF observation.
  if (!ghost_sync_dtm1(ghost_white, "WHITE")) return false;
  if (!ghost_refresh_differential(ghost_pattern, "PATTERN", 1,
                                  "targeted-reverse-mask", true)) {
    return false;
  }
  if (!ghost_refresh_differential(ghost_white, "WHITE", 2,
                                  "target-ending-white-2f", true)) {
    return false;
  }
  if (power_off_after && !ghost_power_off_timed("after target-ending compensation")) return false;
  return true;
}

bool ghost_run_mode(size_t index) {
  if (index >= GHOST_MODE_COUNT) return false;
  const GhostMode& mode = GHOST_MODES[index];
  spi_device = spi_device_nominal;
  soft_stop_requested = false;
  ghost_wait_power_release();

  Serial.printf("\n[X4LAB] MODE %u/%u: %s\n", static_cast<unsigned>(index + 1),
                static_cast<unsigned>(GHOST_MODE_COUNT), mode.name);
  Serial.println("[X4LAB] full visible 800x480; 20-MHz SPI; normal 800x600 geometry; PLL 0x0e/0x0f only");
  Serial.println("[X4LAB] LEFT or RIGHT = immediate reset/halt; POWER or serial s = stop after current refresh");
  Serial.printf("[X4LAB] sequence: pattern settle %lu ms + burn %lu ms; clear settle %lu ms; observation %lu ms\n",
                static_cast<unsigned long>(GHOST_SETTLE_MS),
                static_cast<unsigned long>(GHOST_PATTERN_HOLD_MS),
                static_cast<unsigned long>(GHOST_SETTLE_MS),
                static_cast<unsigned long>(GHOST_OBSERVE_MS));

  if (!ghost_prepare_common_history()) return false;
  if (soft_stop_requested) {
    Serial.println("[X4LAB] mode stopped during common sequence");
    ghost_wait_power_release();
    return true;
  }

  switch (mode.action) {
    case GhostAction::PoweredOn:
      if (!ghost_hold("powered-on-no-maintenance", GHOST_OBSERVE_MS, false)) return false;
      break;

    case GhostAction::PanelOff:
      if (!ghost_power_off_timed("after white settle")) return false;
      if (!ghost_hold("panel-off", GHOST_OBSERVE_MS, false)) return false;
      break;

    case GhostAction::RawInverseOn:
      if (!ghost_refresh_frame(ghost_inverse, "INVERSE_PATTERN", 1, "raw-inverse-1f")) return false;
      if (!ghost_refresh_frame(ghost_white, "WHITE", 1, "raw-inverse-final-white-1f")) return false;
      if (!ghost_hold("raw-inverse-powered-on", GHOST_OBSERVE_MS, false)) return false;
      break;

    case GhostAction::MaintenanceAt30s:
      if (!ghost_hold("powered-on-maintenance-at-30s", GHOST_OBSERVE_MS, true)) return false;
      break;

    case GhostAction::SyncOldOn:
      if (!ghost_sync_dtm1(ghost_white, "WHITE")) return false;
      if (!ghost_hold("dtm1-synced-powered-on", GHOST_OBSERVE_MS, false)) return false;
      break;

    case GhostAction::SyncOldOff:
      if (!ghost_sync_dtm1(ghost_white, "WHITE") ||
          !ghost_power_off_timed("DTM1 white synchronized")) {
        return false;
      }
      if (!ghost_hold("dtm1-synced-panel-off", GHOST_OBSERVE_MS, false)) return false;
      break;

