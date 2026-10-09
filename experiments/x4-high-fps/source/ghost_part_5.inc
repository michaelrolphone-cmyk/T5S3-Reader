bool observe_mode(size_t index, GhostAction action) {
  const int64_t started = esp_timer_get_time();
  int64_t next_log_us = 0;
  bool maintenance_done = false;
  bool pon_done = action != GhostAction::PowerOffThenPowerOn;

  Serial.printf("[X4LAB] OBSERVE begin mode=%u duration_ms=%lu panel=%s DTM1=%s DTM2=white\n",
                static_cast<unsigned>(index + 1), static_cast<unsigned long>(GHOST_OBSERVE_MS),
                screen_powered ? "ON" : "OFF", action == GhostAction::SyncDtm1 ||
                action == GhostAction::SyncDtm1AndPowerOff ? "white" : "pattern/stale");

  while (!soft_stop_requested) {
    const int64_t elapsed_us = esp_timer_get_time() - started;
    const uint32_t elapsed_ms = static_cast<uint32_t>(elapsed_us / 1000);
    if (elapsed_ms >= GHOST_OBSERVE_MS) break;
    if (observation_stop_requested()) break;

    if (action == GhostAction::MaintenanceAtThirtySeconds && !maintenance_done &&
        elapsed_ms >= GHOST_MAINTENANCE_MS) {
      uint32_t wave_us = 0;
      Serial.println("[X4LAB] OBSERVE issuing one full-visible resident maintenance DRF at t=30s");
      if (!resident_pulse("30-second-maintenance", &wave_us)) return false;
      maintenance_done = true;
      Serial.printf("[X4LAB] OBSERVE maintenance complete wave=%lu us\n", static_cast<unsigned long>(wave_us));
    }

    if (action == GhostAction::PowerOffThenPowerOn && !pon_done &&
        elapsed_ms >= GHOST_MAINTENANCE_MS) {
      uint32_t pon_us = 0;
      Serial.println("[X4LAB] OBSERVE issuing PON only at t=30s; no DTM upload and no DRF");
      if (!ghost_power_on(&pon_us)) return false;
      pon_done = true;
      Serial.printf("[X4LAB] OBSERVE PON-only transition complete pon=%lu us\n",
                    static_cast<unsigned long>(pon_us));
    }

    if (elapsed_us >= next_log_us) {
      Serial.printf("[X4LAB] OBSERVE mode=%u t=%lu ms panel=%s busy_n=%d maintenance=%s pon_only=%s\n",
                    static_cast<unsigned>(index + 1), static_cast<unsigned long>(elapsed_ms),
                    screen_powered ? "ON" : "OFF", gpio_get_level(PIN_EPD_BUSY),
                    maintenance_done ? "done" : "none", pon_done ? "done/not-applicable" : "pending");
      next_log_us += static_cast<int64_t>(GHOST_LOG_INTERVAL_MS) * 1000;
    }
    delay(10);
  }

  if (gpio_get_level(PIN_BTN_POWER) == 0) wait_power_release();
  const uint32_t elapsed_ms = static_cast<uint32_t>((esp_timer_get_time() - started) / 1000);
  Serial.printf("[X4LAB] OBSERVE complete mode=%u elapsed_ms=%lu panel=%s busy_n=%d\n",
                static_cast<unsigned>(index + 1), static_cast<unsigned long>(elapsed_ms),
                screen_powered ? "ON" : "OFF", gpio_get_level(PIN_EPD_BUSY));
  Serial.println("[X4LAB] INSPECT NOW: the white target is retained. Starting another mode will overwrite it.");
  return true;
}

bool apply_mode_action(size_t index, GhostAction action) {
  switch (action) {
    case GhostAction::PoweredIdle:
      Serial.println("[X4LAB] ACTION powered idle; no DTM1 sync, no POF, no maintenance");
      return true;
    case GhostAction::PowerOff: {
      Serial.println("[X4LAB] ACTION POF with stale pattern in DTM1");
      uint32_t pof_us = 0;
      return ghost_power_off(&pof_us);
    }
    case GhostAction::InvertThenWhite:
      Serial.println("[X4LAB] ACTION one inverse-pattern frame followed immediately by one white frame");
      return absolute_target_refresh(ghost_inverse, 1, "inverse-one-frame") &&
             absolute_target_refresh(ghost_white, 1, "white-restore-one-frame");
    case GhostAction::SyncDtm1:
      Serial.println("[X4LAB] ACTION synchronize DTM1 to white; leave analog panel power ON");
      return synchronize_dtm1_white();
    case GhostAction::SyncDtm1AndPowerOff: {
      Serial.println("[X4LAB] ACTION synchronize DTM1 to white, then POF");
      uint32_t pof_us = 0;
      return synchronize_dtm1_white() && ghost_power_off(&pof_us);
    }
    case GhostAction::MaintenanceAtThirtySeconds:
      Serial.println("[X4LAB] ACTION powered idle; one resident DRF will occur at 30 seconds");
      return true;
    case GhostAction::TargetEndingCompensation:
      Serial.println("[X4LAB] ACTION inverse one-frame pulse followed by target-ending two-frame white pulse");
      return absolute_target_refresh(ghost_inverse, 1, "compensation-reverse-one-frame") &&
             absolute_target_refresh(ghost_white, 2, "compensation-final-white-two-frame");
    case GhostAction::ResetHeld:
      Serial.println("[X4LAB] ACTION assert controller RESET low for the complete observation interval");
      gpio_set_level(PIN_EPD_CS, 1);
      gpio_set_level(PIN_EPD_RST, 0);
      screen_powered = false;
      return true;
    case GhostAction::PowerOffThenPowerOn: {
      Serial.println("[X4LAB] ACTION POF now; PON-only transition will occur at 30 seconds");
      uint32_t pof_us = 0;
      return ghost_power_off(&pof_us);
    }
  }
  Serial.printf("[X4LAB] invalid ghost action for mode=%u\n", static_cast<unsigned>(index + 1));
  return false;
}

bool run_ghost_mode(size_t index) {
  if (index >= GHOST_MODE_COUNT) return false;
  const GhostMode& mode = GHOST_MODES[index];
  spi_device = spi_device_nominal;
  soft_stop_requested = false;
  wait_power_release();

  Serial.printf("\n[X4LAB] MODE %u/%u: %s\n", static_cast<unsigned>(index + 1),
                static_cast<unsigned>(GHOST_MODE_COUNT), mode.name);
  Serial.println("[X4LAB] LEFT OR RIGHT SIDE BUTTON = IMMEDIATE HARD ABORT");
  Serial.println("[X4LAB] POWER during the 60-second observation ends that observation early");
  Serial.println("[X4LAB] fixed work: full 800x480 pattern, full white clear, 2.3s pattern drive, 2.3s white drive");
  Serial.println("[X4LAB] transport=20MHz geometry=800x600 visible_offset=120 TCON=untouched voltages=untouched");

  if (!prepare_pattern_then_clear()) return false;
  if (!apply_mode_action(index, mode.action)) return false;
  if (!observe_mode(index, mode.action)) return false;

  soft_stop_requested = false;
  wait_power_release();
  return true;
}

bool consume_ghost_power_press() {
  static bool previous = true;
  static int64_t changed_at = 0;
  const bool current = gpio_get_level(PIN_BTN_POWER) != 0;
  const int64_t now = esp_timer_get_time();
  bool pressed = false;
  if (current != previous && now - changed_at > 30000) {
    changed_at = now;
    previous = current;
    pressed = !current;
  }
  return pressed;
}

void print_ghost_modes() {
  Serial.println("[X4LAB] diagnostic matrix:");
  for (size_t i = 0; i < GHOST_MODE_COUNT; ++i) {
    Serial.printf("[X4LAB]   %u: %s\n", static_cast<unsigned>(i + 1), GHOST_MODES[i].name);
  }
}

void poll_ghost_serial() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'q' || c == 'Q' || c == 'x' || c == 'X' || c == 27) {
      abort_pin = 0xFE;
      abort_requested = true;
      check_abort();
    }
    if (c == 'n' || c == 'N' || c == ' ') {
      if (!run_ghost_mode(next_mode)) enter_abort_halt("ghost diagnostic mode failed");
      next_mode = (next_mode + 1U) % GHOST_MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u. Inspect retained image, then press POWER or send n.\n",
                    static_cast<unsigned>(next_mode + 1));
    }
    if (c >= '1' && c <= '9') {
      const size_t selected = static_cast<size_t>(c - '1');
      if (selected < GHOST_MODE_COUNT) {
        if (!run_ghost_mode(selected)) enter_abort_halt("selected ghost diagnostic mode failed");
        next_mode = (selected + 1U) % GHOST_MODE_COUNT;
        Serial.printf("[X4LAB] ready; next mode %u. Inspect retained image, then press POWER or send n.\n",
                      static_cast<unsigned>(next_mode + 1));
      }
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.printf("\n[X4LAB] Xteink X4 Pro UC8279 ghost/idle laboratory firmware v%s\n", GHOST_LAB_VERSION);
  Serial.println("[X4LAB] based on stable high-FPS lab v0.1.5 electrical envelope");
  Serial.println("[X4LAB] LEFT OR RIGHT SIDE BUTTON: immediate controller reset + permanent halt");
  Serial.println("[X4LAB] POWER: start next mode; during observation it ends the hold early");
  Serial.println("[X4LAB] q/x/Escape: hard abort over USB serial");
  Serial.println("[X4LAB] no 40/80MHz SPI, compact geometry, TCON, PLL 0x3F, voltage, VCOM-level, booster, or PMIC experiments");

  configure_output(PIN_LIGHT_COOL, 0);
  configure_output(PIN_LIGHT_WARM, 0);
  configure_output(PIN_EPD_CS, 1);
  configure_output(PIN_EPD_DC, 0);
  configure_output(PIN_EPD_SCLK, 0);
  configure_output(PIN_EPD_MOSI, 0);
  configure_output(PIN_EPD_RST, 0);
  setup_abort_inputs();

  if (!probe_controller()) enter_abort_halt("wrong/unknown display controller");
  if (!initialize_spi()) enter_abort_halt("SPI initialization failed");
  spi_device = spi_device_nominal;
  if (!allocate_ghost_frames()) enter_abort_halt("diagnostic frame allocation failed");
  if (!controller_init(PLL_STOCK)) enter_abort_halt("controller initialization failed");
  if (!otp_refresh_and_sync(ghost_white, "initial-white")) enter_abort_halt("initial white baseline failed");

  Serial.printf("[X4LAB] armed for UC8279 LUT id 0x%02x at %d MHz SPI\n", panel_lut_id,
                SPI_CLOCK_HZ / 1000000);
  print_ghost_modes();
  Serial.println("[X4LAB] inspect each retained white screen before starting the next mode");
}

void loop() {
  check_abort();
  poll_ghost_serial();
  if (consume_ghost_power_press()) {
    if (!run_ghost_mode(next_mode)) enter_abort_halt("ghost diagnostic mode failed");
    next_mode = (next_mode + 1U) % GHOST_MODE_COUNT;
    Serial.printf("[X4LAB] ready; next mode %u. Inspect retained image, then press POWER or send n.\n",
                  static_cast<unsigned>(next_mode + 1));
  }
  delay(5);
}
