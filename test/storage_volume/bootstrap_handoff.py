"""Compile the actual bootstrap cleanup; GPIO/controller endpoints are host doubles."""
from pathlib import Path


def header(root: Path) -> str:
    source = (root/'src/platform/SdBootReader.cpp').read_text()
    unmount = source[source.index('esp_err_t unmount() {'):source.index('\nesp_err_t mountDisk() {')]
    return r'''
namespace BootstrapHandoff {
using esp_err_t = int;
using gpio_num_t = uint32_t;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = -2;
constexpr int GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT = 1, GPIO_FLOATING = 2;
constexpr int FR_OK = 0;
constexpr uint32_t X4PRO_PIN_SD_PWR = 5, X4PRO_PIN_SD_CLK = 41;
constexpr uint32_t X4PRO_PIN_SD_CMD = 42, X4PRO_PIN_SD_DAT0 = 40;
static bool operation, closeFailed, diskRegistered = true, mounted = true, hostUp = true;
static unsigned deinitializations, clockParks, inputParks;
static void* owner = reinterpret_cast<void*>(1);
struct Claim { bool held = true; void store(bool value) { held = value; } };
static Claim claimed;
static const char* volume = "boot";
static bool onOwner() { return owner != nullptr; }
static int f_mount(void*, const char*, int) { return FR_OK; }
static int sdmmc_host_deinit() { ++deinitializations; return ESP_OK; }
static int gpio_hold_dis(gpio_num_t pin) {
    assert(pin == X4PRO_PIN_SD_PWR && deinitializations == 1);
    card_sleep_off = false; return ESP_OK;
}
static int gpio_set_direction(gpio_num_t pin, int mode) {
    assert((pin == X4PRO_PIN_SD_PWR || pin == X4PRO_PIN_SD_CLK) ? mode == GPIO_MODE_OUTPUT : mode == GPIO_MODE_INPUT);
    return ESP_OK;
}
static int gpio_set_level(gpio_num_t pin, int level) {
    if (pin == X4PRO_PIN_SD_PWR) { if (!card_sleep_off) card_power_off = level != 0; }
    else { assert(pin == X4PRO_PIN_SD_CLK && level == 0); ++clockParks; }
    return ESP_OK;
}
static int gpio_set_pull_mode(gpio_num_t pin, int mode) {
    assert((pin == X4PRO_PIN_SD_CMD || pin == X4PRO_PIN_SD_DAT0) && mode == GPIO_FLOATING);
    ++inputParks; return ESP_OK;
}
static int gpio_hold_en(gpio_num_t pin) {
    assert(pin == X4PRO_PIN_SD_PWR && card_power_off && clockParks == 1 && inputParks == 2);
    card_sleep_off = true; return ESP_OK;
}
''' + unmount + '\n} // namespace BootstrapHandoff\n'
