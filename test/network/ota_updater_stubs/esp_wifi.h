#pragma once
#include <string>
#include <vector>
using esp_err_t = int;
constexpr int WIFI_PS_NONE = 0;
constexpr int WIFI_PS_MIN_MODEM = 1;
inline std::vector<std::string> ota_install_calls;
inline int ota_wifi_power_save = WIFI_PS_MIN_MODEM;
inline esp_err_t ota_wifi_restore_result = 0;
inline esp_err_t esp_wifi_set_ps(int mode) {
  ota_install_calls.push_back(mode == WIFI_PS_NONE ? "ps:none" : "ps:min-modem");
  const esp_err_t result = mode == WIFI_PS_MIN_MODEM ? ota_wifi_restore_result : 0;
  if (result == 0) ota_wifi_power_save = mode;
  return result;
}
