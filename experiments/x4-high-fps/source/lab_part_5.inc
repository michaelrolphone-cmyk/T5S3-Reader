    const uint32_t next_frame = current_frame + 1;
    const int64_t frame_start = esp_timer_get_time();
    const int64_t upload_start = frame_start;
    if (!stream_mode_plane(mode, CMD_DTM2, next_frame)) {
      release_frame_cache();
      return false;
    }
    const uint32_t upload_us = static_cast<uint32_t>(esp_timer_get_time() - upload_start);

    uint32_t waveform_us = 0;
    if (!begin_refresh(mode, &waveform_us)) {
      release_frame_cache();
      return false;
    }

    const int64_t sync_start = esp_timer_get_time();
    if (!stream_mode_plane(mode, CMD_DTM1, next_frame)) {
      release_frame_cache();
      return false;
    }
    const uint32_t sync_us = static_cast<uint32_t>(esp_timer_get_time() - sync_start);
    const uint32_t total_us = static_cast<uint32_t>(esp_timer_get_time() - frame_start);
    current_frame = next_frame;

    sum_total += total_us;
    sum_waveform += waveform_us;
    sum_upload += upload_us;
    sum_sync += sync_us;
    ++completed;
    const uint32_t fps_x100 = total_us == 0 ? 0 : static_cast<uint32_t>(100000000ULL / total_us);
    Serial.printf("[X4LAB] frame=%u upload=%lu us wave=%lu us sync=%lu us total=%lu us fps=%lu.%02lu\n",
                  static_cast<unsigned>(n + 1), static_cast<unsigned long>(upload_us),
                  static_cast<unsigned long>(waveform_us), static_cast<unsigned long>(sync_us),
                  static_cast<unsigned long>(total_us), static_cast<unsigned long>(fps_x100 / 100),
                  static_cast<unsigned long>(fps_x100 % 100));
    // loopTask outranks IDLE0. Block outside the measured frame interval so
    // the task watchdog remains active without contaminating panel timing.
    delay(1);
    check_abort();
  }

  if (completed != 0) {
    const uint64_t average = sum_total / completed;
    const uint64_t fps_x100 = average == 0 ? 0 : 100000000ULL / average;
    Serial.printf("[X4LAB] RESULT mode=%u frames=%u avg_upload=%llu us avg_wave=%llu us avg_sync=%llu us "
                  "avg_total=%llu us avg_fps=%llu.%02llu\n",
                  static_cast<unsigned>(index + 1), static_cast<unsigned>(completed), sum_upload / completed,
                  sum_waveform / completed, sum_sync / completed, average, fps_x100 / 100, fps_x100 % 100);
  } else {
    Serial.printf("[X4LAB] mode=%u stopped before first frame\n", static_cast<unsigned>(index + 1));
  }
  release_frame_cache();
  while (gpio_get_level(PIN_BTN_POWER) == 0) delay(10);
  return true;
}

bool consume_power_press() {
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

void poll_serial() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'q' || c == 'Q' || c == 'x' || c == 'X') {
      abort_pin = 0xFE;
      abort_requested = true;
      check_abort();
    }
    if (c == 'n' || c == 'N' || c == ' ') {
      if (!run_mode(next_mode)) enter_abort_halt("test mode failed");
      next_mode = (next_mode + 1) % MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u. Press POWER or send n.\n", static_cast<unsigned>(next_mode + 1));
    }
    if (c >= '1' && c <= '0' + static_cast<int>(MODE_COUNT)) {
      const size_t selected = static_cast<size_t>(c - '1');
      if (!run_mode(selected)) enter_abort_halt("selected test mode failed");
      next_mode = (selected + 1) % MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u. Press POWER or send n.\n", static_cast<unsigned>(next_mode + 1));
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.printf("\n[X4LAB] Xteink X4 Pro UC8279 high-FPS standalone laboratory firmware v%s\n", LAB_VERSION);
  Serial.println("[X4LAB] LEFT OR RIGHT SIDE BUTTON: immediate controller reset + permanent halt");
  Serial.println("[X4LAB] POWER: start next mode; during a mode, request stop after current refresh");
  Serial.println("[X4LAB] task watchdog retained; IDLE0 is serviced between measured frames");
  Serial.println("[X4LAB] matrix: legacy references, cached partial RAM writes, Bayer 5/4/3/2-frame, full panel");
  Serial.println("[X4LAB] 2x target uses the documented 200-Hz PLL ceiling plus cached-window transport");

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
  if (!controller_init(PLL_STOCK)) enter_abort_halt("controller initialization failed");
  if (!clean_baseline(160, 160, Pattern::BinaryGrid)) enter_abort_halt("initial clean baseline failed");

  Serial.printf("[X4LAB] armed for UC8279 LUT id 0x%02x at %d MHz SPI.\n", panel_lut_id, SPI_CLOCK_HZ / 1000000);
  Serial.printf("[X4LAB] %u modes available. Press POWER or send n; send 1-%u to select; q aborts.\n",
                static_cast<unsigned>(MODE_COUNT), static_cast<unsigned>(MODE_COUNT));
}

void loop() {
  check_abort();
  poll_serial();
  if (consume_power_press()) {
    if (!run_mode(next_mode)) enter_abort_halt("test mode failed");
    next_mode = (next_mode + 1) % MODE_COUNT;
    Serial.printf("[X4LAB] ready; next mode %u. Press POWER or send n.\n", static_cast<unsigned>(next_mode + 1));
  }
  delay(5);
}

