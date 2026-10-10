bool visual_consume_power_press() {
  static bool previous = true;
  static int64_t changed = 0;
  const bool current = gpio_get_level(PIN_BTN_POWER) != 0;
  const int64_t now = esp_timer_get_time();
  bool pressed = false;
  if (current != previous && now - changed > 30000) {
    changed = now; previous = current; pressed = !current;
  }
  return pressed;
}

void visual_print_modes() {
  Serial.println("[X4LAB] visual matrix keys:");
  for (size_t i = 0; i < VISUAL_MODE_COUNT; ++i)
    Serial.printf("[X4LAB]   %c = %u: %s\n", visual_key(i), static_cast<unsigned>(i + 1), VISUAL_MODES[i].name);
}

void visual_poll_serial() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'q' || c == 'Q' || c == 'x' || c == 'X' || c == 27) {
      abort_pin = 0xFE; abort_requested = true; check_abort();
    }
    if (c == 'n' || c == 'N' || c == ' ') {
      if (!visual_run_mode(visual_next_mode)) enter_abort_halt("visual mode failed");
      visual_next_mode = (visual_next_mode + 1U) % VISUAL_MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u (%c)\n",
                    static_cast<unsigned>(visual_next_mode + 1), visual_key(visual_next_mode));
      continue;
    }
    const int selected = visual_key_index(c);
    if (selected >= 0 && static_cast<size_t>(selected) < VISUAL_MODE_COUNT) {
      if (!visual_run_mode(static_cast<size_t>(selected))) enter_abort_halt("selected visual mode failed");
      visual_next_mode = (static_cast<size_t>(selected) + 1U) % VISUAL_MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u (%c)\n",
                    static_cast<unsigned>(visual_next_mode + 1), visual_key(visual_next_mode));
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.printf("\n[X4LAB] Xteink X4 Pro UC8279 visual contrast laboratory v%s\n", VISUAL_LAB_VERSION);
  Serial.println("[X4LAB] LEFT OR RIGHT SIDE BUTTON: immediate controller reset + permanent halt");
  Serial.println("[X4LAB] POWER: next mode; during a mode stop after current refresh");
  Serial.println("[X4LAB] transport=20MHz; geometry=800x600/+120; no compact scan or SPI overclock");
  Serial.println("[X4LAB] top witness is black-on-white; bottom witness is white-on-black");

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
  if (!controller_init(PLL_STOCK)) enter_abort_halt("controller initialization failed");
  Serial.printf("[X4LAB] armed UC8279 LUT id=0x%02x, modes=%u\n", panel_lut_id,
                static_cast<unsigned>(VISUAL_MODE_COUNT));
  visual_print_modes();
  Serial.println("[X4LAB] press POWER or send n to run mode 1; q/x/Escape hard-aborts");
}

void loop() {
  check_abort();
  visual_poll_serial();
  if (visual_consume_power_press()) {
    if (!visual_run_mode(visual_next_mode)) enter_abort_halt("visual mode failed");
    visual_next_mode = (visual_next_mode + 1U) % VISUAL_MODE_COUNT;
    Serial.printf("[X4LAB] ready; next mode %u (%c)\n",
                  static_cast<unsigned>(visual_next_mode + 1), visual_key(visual_next_mode));
  }
  delay(5);
}
