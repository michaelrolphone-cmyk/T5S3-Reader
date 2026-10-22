size_t next_mode = 0;
uint32_t current_frame = 0;

void IRAM_ATTR hard_abort_isr(void* arg) {
  abort_pin = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
  abort_requested = true;
  // Stop command acceptance immediately. GPIOs are all below 32 on ESP32-S3.
  GPIO.out_w1tc = (1UL << PIN_EPD_RST) | (1UL << PIN_EPD_SCLK) | (1UL << PIN_EPD_MOSI) |
                      (1UL << PIN_EPD_DC);
  GPIO.out_w1ts = (1UL << PIN_EPD_CS);
}

[[noreturn]] void enter_abort_halt(const char* reason) {
  gpio_set_level(PIN_EPD_CS, 1);
  gpio_set_level(PIN_EPD_RST, 0);
  gpio_set_level(PIN_EPD_SCLK, 0);
  gpio_set_level(PIN_EPD_MOSI, 0);
  gpio_set_level(PIN_LIGHT_COOL, 0);
  gpio_set_level(PIN_LIGHT_WARM, 0);
  Serial.printf("\n[X4LAB] HARD ABORT: %s; pin=%lu. Panel reset is held LOW. Reboot to continue.\n", reason,
                static_cast<unsigned long>(abort_pin));
  Serial.flush();
  while (true) delay(1000);
}

inline void check_abort() {
  if (abort_requested) enter_abort_halt("physical/serial abort");
}

bool configure_output(gpio_num_t pin, int level) {
  gpio_config_t cfg{};
  cfg.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&cfg) != ESP_OK) return false;
  return gpio_set_level(pin, level) == ESP_OK;
}

bool configure_input(gpio_num_t pin, gpio_pullup_t pullup, gpio_int_type_t interrupt) {
  gpio_config_t cfg{};
  cfg.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = pullup;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = interrupt;
  return gpio_config(&cfg) == ESP_OK;
}

void setup_abort_inputs() {
  configure_input(PIN_BTN_LEFT, GPIO_PULLUP_ENABLE, GPIO_INTR_NEGEDGE);
  configure_input(PIN_BTN_RIGHT, GPIO_PULLUP_ENABLE, GPIO_INTR_NEGEDGE);
  configure_input(PIN_BTN_POWER, GPIO_PULLUP_ENABLE, GPIO_INTR_DISABLE);
  esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("[X4LAB] gpio_install_isr_service failed: %s\n", esp_err_to_name(err));
    enter_abort_halt("abort ISR unavailable");
  }
  if (gpio_isr_handler_add(PIN_BTN_LEFT, hard_abort_isr,
                           reinterpret_cast<void*>(static_cast<uintptr_t>(PIN_BTN_LEFT))) != ESP_OK ||
      gpio_isr_handler_add(PIN_BTN_RIGHT, hard_abort_isr,
                           reinterpret_cast<void*>(static_cast<uintptr_t>(PIN_BTN_RIGHT))) != ESP_OK) {
    enter_abort_halt("abort button setup failed");
  }
  if (gpio_get_level(PIN_BTN_LEFT) == 0 || gpio_get_level(PIN_BTN_RIGHT) == 0) {
    abort_pin = gpio_get_level(PIN_BTN_LEFT) == 0 ? PIN_BTN_LEFT : PIN_BTN_RIGHT;
    enter_abort_halt("side button held during boot");
  }
}

void bitbang_delay() {
  for (unsigned i = 0; i < 48; ++i) asm volatile("nop" ::: "memory");
}

void bitbang_write(uint8_t value) {
  for (int bit = 7; bit >= 0; --bit) {
    gpio_set_level(PIN_EPD_MOSI, (value >> bit) & 1U);
    gpio_set_level(PIN_EPD_SCLK, 1);
    bitbang_delay();
    gpio_set_level(PIN_EPD_SCLK, 0);
  }
}

uint8_t bitbang_read() {
  uint8_t value = 0;
  for (int bit = 0; bit < 8; ++bit) {
    bitbang_delay();
    value = static_cast<uint8_t>((value << 1) | (gpio_get_level(PIN_EPD_MOSI) ? 1U : 0U));
    gpio_set_level(PIN_EPD_SCLK, 1);
    bitbang_delay();
    gpio_set_level(PIN_EPD_SCLK, 0);
  }
  return value;
}

void read_controller_register(uint8_t command, uint8_t* out, size_t length) {
  configure_output(PIN_EPD_MOSI, 0);
  gpio_set_level(PIN_EPD_DC, 0);
  gpio_set_level(PIN_EPD_CS, 0);
  bitbang_write(command);
  gpio_set_level(PIN_EPD_DC, 1);
  configure_input(PIN_EPD_MOSI, GPIO_PULLUP_ENABLE, GPIO_INTR_DISABLE);
  bitbang_delay();
  for (size_t i = 0; i < length; ++i) out[i] = bitbang_read();
  gpio_set_level(PIN_EPD_CS, 1);
  configure_output(PIN_EPD_MOSI, 0);
}

bool wait_busy_high(uint32_t timeout_ms, const char* stage) {
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
  while (gpio_get_level(PIN_EPD_BUSY) == 0) {
    check_abort();
    if (esp_timer_get_time() >= deadline) {
      Serial.printf("[X4LAB] timeout waiting BUSY_N high: %s\n", stage);
      return false;
    }
    delayMicroseconds(100);
  }
  return true;
}

bool probe_controller() {
  check_abort();
  configure_output(PIN_EPD_CS, 1);
  configure_output(PIN_EPD_DC, 0);
  configure_output(PIN_EPD_SCLK, 0);
  configure_output(PIN_EPD_MOSI, 0);
  configure_output(PIN_EPD_RST, 1);
  configure_input(PIN_EPD_BUSY, GPIO_PULLUP_DISABLE, GPIO_INTR_DISABLE);

  gpio_set_level(PIN_EPD_RST, 0);
  delay(50);
  check_abort();
  gpio_set_level(PIN_EPD_RST, 1);
  check_abort();
  delay(50);
  check_abort();
  if (!wait_busy_high(1000, "probe reset")) return false;

  uint8_t flag = 0;
  uint8_t version[5]{};
