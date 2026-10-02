#include <esp_err.h>
#include "network/HttpDownloader.h"
// This offline core-service profile has no foreground app launcher or network
// provider. Unsupported surfaces fail closed; no successful stand-ins. Provider
// contexts and scheduler use the unchanged production NativeStreamBridge.
bool HttpDownloader::fetchUrl(const std::string&, Stream&, const std::string&, const std::string&) { return false; }
// Arduino 2.0.14 otherwise auto-erases unfamiliar/newer NVS during initArduino.
// This profile has no NVS consumer: report unsupported, never fake success or
// touch another firmware's settings. Applies to this link only, no SDK edits.
extern "C" esp_err_t __wrap_nvs_flash_init() { return ESP_ERR_NOT_SUPPORTED; }
