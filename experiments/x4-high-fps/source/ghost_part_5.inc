    case GhostAction::TargetCompOn:
      if (!ghost_target_compensation(false)) return false;
      if (!ghost_hold("target-compensated-powered-on", GHOST_OBSERVE_MS, false)) return false;
      break;

    case GhostAction::TargetCompOff:
      if (!ghost_target_compensation(true)) return false;
      if (!ghost_hold("target-compensated-panel-off", GHOST_OBSERVE_MS, false)) return false;
      break;

    case GhostAction::PowerCycleNoRefresh: {
      if (!ghost_sync_dtm1(ghost_white, "WHITE") ||
          !ghost_power_off_timed("power-cycle no-DRF test")) {
        return false;
      }
      if (!ghost_hold("power-cycle-off-half", GHOST_SPLIT_MS, false)) return false;
      if (soft_stop_requested) break;
      uint32_t pon_us = 0;
      if (!ghost_power_on_timed("power-cycle no-DRF reactivation", &pon_us)) return false;
      Serial.printf("[X4LAB] PON_NO_DRF measured_total=%lu us; no PTL, DTM or DRF command follows\n",
                    static_cast<unsigned long>(pon_us));
      if (!ghost_hold("power-cycle-on-no-DRF-half", GHOST_SPLIT_MS, false)) return false;
      break;
    }
  }

  Serial.printf("[X4LAB] RESULT mode=%u powered=%u BUSY_N=%d DTM1=%s DTM2=%s stopped=%u\n",
                static_cast<unsigned>(index + 1), screen_powered ? 1U : 0U,
                gpio_get_level(PIN_EPD_BUSY), ghost_dtm1_state, ghost_dtm2_state,
                soft_stop_requested ? 1U : 0U);
  Serial.println("[X4LAB] image/state left untouched for inspection; next mode begins with reset + OTP white clean");
  ghost_wait_power_release();
  return true;
}

bool ghost_consume_power_press() {
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

void ghost_poll_menu() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'q' || c == 'Q' || c == 'x' || c == 'X' || c == 27) {
      abort_pin = 0xFE;
      abort_requested = true;
      check_abort();
    }
    if (c == 'n' || c == 'N' || c == ' ') {
      if (!ghost_run_mode(ghost_next_mode)) enter_abort_halt("ghosting mode failed");
      ghost_next_mode = (ghost_next_mode + 1U) % GHOST_MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u. Press POWER or send n.\n",
                    static_cast<unsigned>(ghost_next_mode + 1U));
    }
    if (c >= '1' && c <= '9') {
      const size_t selected = static_cast<size_t>(c - '1');
      if (!ghost_run_mode(selected)) enter_abort_halt("selected ghosting mode failed");
      ghost_next_mode = (selected + 1U) % GHOST_MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u. Press POWER or send n.\n",
                    static_cast<unsigned>(ghost_next_mode + 1U));
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.printf("\n[X4LAB] Xteink X4 Pro UC8279 ghosting diagnostic firmware v%s\n", GHOST_LAB_VERSION);
  Serial.println("[X4LAB] based directly on stable v0.1.5; destructive v0.1.6 mechanisms are absent");
  Serial.println("[X4LAB] LEFT OR RIGHT: immediate controller reset + permanent halt");
  Serial.println("[X4LAB] POWER during a mode or serial s: stop after current physical refresh");
  Serial.println("[X4LAB] q/x/Escape: hard abort; n/space: next mode; 1-9: select mode");
  Serial.println("[X4LAB] every mode drives the complete visible 800x480 region at 20 MHz");

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
  if (!ghost_allocate_frames()) enter_abort_halt("diagnostic frame allocation failed");
  if (!ghost_otp_clean_white()) enter_abort_halt("initial OTP white clean failed");

  Serial.printf("[X4LAB] armed UC8279 LUT id=0x%02x nominal_spi=%dMHz modes=%u\n", panel_lut_id,
                SPI_CLOCK_HZ / 1000000, static_cast<unsigned>(GHOST_MODE_COUNT));
  for (size_t i = 0; i < GHOST_MODE_COUNT; ++i)
    Serial.printf("[X4LAB]   %u: %s\n", static_cast<unsigned>(i + 1U), GHOST_MODES[i].name);
  Serial.println("[X4LAB] Press POWER or send n to run mode 1.");
}

void loop() {
  check_abort();
  ghost_poll_menu();
