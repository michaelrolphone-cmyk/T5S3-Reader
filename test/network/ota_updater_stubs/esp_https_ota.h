#pragma once
#include "esp_http_client.h"
#include "esp_wifi.h"
using esp_https_ota_handle_t = void*;
struct esp_https_ota_config_t {
  const esp_http_client_config_t* http_config;
  esp_err_t (*http_client_init_cb)(esp_http_client_handle_t);
};
constexpr esp_err_t ESP_ERR_HTTPS_OTA_IN_PROGRESS = 1;
inline int esp_https_ota_begin_count = 0;
inline size_t ota_https_ota_read_bytes = 0;
inline esp_err_t ota_https_ota_begin_result = ESP_OK;
inline esp_err_t ota_https_ota_perform_result = ESP_OK;
inline bool ota_https_ota_complete = true;
inline esp_err_t ota_https_ota_finish_result = ESP_OK;
inline esp_err_t esp_https_ota_begin(const esp_https_ota_config_t*, esp_https_ota_handle_t* handle) {
  ++esp_https_ota_begin_count;
  ota_install_calls.push_back("ota:begin");
  if (ota_https_ota_begin_result == ESP_OK) *handle = reinterpret_cast<void*>(1);
  return ota_https_ota_begin_result;
}
inline esp_err_t esp_https_ota_perform(esp_https_ota_handle_t) {
  ota_install_calls.push_back("ota:perform");
  return ota_https_ota_perform_result;
}
inline size_t esp_https_ota_get_image_len_read(esp_https_ota_handle_t) { return ota_https_ota_read_bytes; }
inline bool esp_https_ota_is_complete_data_received(esp_https_ota_handle_t) {
  ota_install_calls.push_back("ota:complete");
  return ota_https_ota_complete;
}
inline esp_err_t esp_https_ota_finish(esp_https_ota_handle_t) {
  ota_install_calls.push_back("ota:finish");
  return ota_https_ota_finish_result;
}
