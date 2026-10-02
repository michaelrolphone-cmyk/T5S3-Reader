#pragma once
#include <cstddef>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr int HTTP_EVENT_ON_DATA = 1;
struct esp_http_client_event_t { int event_id; void* data; int data_len; void* user_data; };
using esp_http_client_handle_t = void*;
struct esp_http_client_config_t {
  const char* url;
  const char* cert_pem;
  int timeout_ms;
  esp_err_t (*event_handler)(esp_http_client_event_t*);
  int buffer_size;
  int buffer_size_tx;
  void* user_data;
  bool skip_cert_common_name_check;
  bool keep_alive_enable;
};
inline esp_err_t ota_http_perform_result = ESP_OK;
inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*) { return reinterpret_cast<void*>(1); }
inline esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*) { return ESP_OK; }
inline esp_err_t esp_http_client_perform(esp_http_client_handle_t) { return ota_http_perform_result; }
inline esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) { return ESP_OK; }
inline const char* esp_err_to_name(esp_err_t) { return "mock"; }
