char focused_key(size_t index) {
  if (index < 9) return static_cast<char>('1' + index);
  return static_cast<char>('a' + index - 9);
}
int focused_key_index(int c) {
  if (c >= '1' && c <= '9') return c - '1';
  if (c >= 'a' && c <= 'p') return 9 + c - 'a';
  if (c >= 'A' && c <= 'P') return 9 + c - 'A';
  return -1;
}

bool focused_consume_power_press() {
  static bool previous = true;
  static int64_t changed = 0;
  const bool current = gpio_get_level(PIN_BTN_POWER) != 0;
  const int64_t now = esp_timer_get_time();
  bool pressed = false;
  if (current != previous && now - changed > 30000) {
    changed = now;
    previous = current;
    pressed = !current;
  }
  return pressed;
}

void focused_print_modes() {
  Serial.println("[X4LAB] focused matrix:");
  for (size_t i = 0; i < FOCUSED_MODE_COUNT; ++i)
    Serial.printf("[X4LAB]   %c = %u: %s\n", focused_key(i), static_cast<unsigned>(i + 1), FOCUSED_MODES[i].name);
}

void focused_poll_serial() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'q' || c == 'Q' || c == 'x' || c == 'X' || c == 27) {
      abort_pin = 0xFE;
      abort_requested = true;
      check_abort();
    }
    if (c == 'n' || c == 'N' || c == ' ') {
      if (!focused_run_mode(focused_next_mode)) enter_abort_halt("focused mode failed");
      focused_next_mode = (focused_next_mode + 1U) % FOCUSED_MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u (%c)\n", static_cast<unsigned>(focused_next_mode + 1), focused_key(focused_next_mode));
      continue;
    }
    const int selected = focused_key_index(c);
    if (selected >= 0 && static_cast<size_t>(selected) < FOCUSED_MODE_COUNT) {
      if (!focused_run_mode(static_cast<size_t>(selected))) enter_abort_halt("selected focused mode failed");
      focused_next_mode = (static_cast<size_t>(selected) + 1U) % FOCUSED_MODE_COUNT;
      Serial.printf("[X4LAB] ready; next mode %u (%c)\n", static_cast<unsigned>(focused_next_mode + 1), focused_key(focused_next_mode));
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.printf("\n[X4LAB] Xteink X4 Pro UC8279 directional-balance laboratory v%s\n", FOCUSED_LAB_VERSION);
  Serial.println("[X4LAB] derived from stable v0.1.5 and visual matrix v0.1.8 findings");
  Serial.println("[X4LAB] LEFT OR RIGHT: immediate controller reset + permanent halt");
  Serial.println("[X4LAB] POWER: next mode; during a mode stop after current logical frame");
  Serial.println("[X4LAB] transport=20MHz geometry=800x600/+120 VCOM=factory PLL=200Hz");

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

  Serial.printf("[X4LAB] armed UC8279 LUT id=0x%02x modes=%u\n", panel_lut_id,
                static_cast<unsigned>(FOCUSED_MODE_COUNT));
  focused_print_modes();
  Serial.println("[X4LAB] press POWER or send n to run mode 1; q/x/Escape hard-aborts");
}

void loop() {
  check_abort();
  focused_poll_serial();
  if (focused_consume_power_press()) {
    if (!focused_run_mode(focused_next_mode)) enter_abort_halt("focused mode failed");
    focused_next_mode = (focused_next_mode + 1U) % FOCUSED_MODE_COUNT;
    Serial.printf("[X4LAB] ready; next mode %u (%c)\n", static_cast<unsigned>(focused_next_mode + 1), focused_key(focused_next_mode));
  }
  delay(5);
}
