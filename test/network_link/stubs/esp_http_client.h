#pragma once
#include "NetworkTestSupport.h"
constexpr int HTTP_EVENT_ON_DATA = 1, HTTP_TRANSPORT_UNKNOWN = 0, HTTP_METHOD_POST = 1, HTTP_METHOD_GET = 0;
struct esp_http_client_event_t { int event_id; void* user_data; int data_len; void* data; };
struct esp_http_client_config_t {
  const char* url = nullptr;
  int transport_type = 0;
  const char* cert_pem = nullptr;
  int timeout_ms = 0;
  esp_err_t (*event_handler)(esp_http_client_event_t*) = nullptr;
  int buffer_size = 0, buffer_size_tx = 0;
  void* user_data = nullptr;
  bool skip_cert_common_name_check = false, keep_alive_enable = false;
};
using esp_http_client_handle_t = void*;
inline void* esp_http_client_init(const esp_http_client_config_t*) {
  Fixture::events.push_back("idf.init"); return Fixture::httpInit ? &Fixture::httpInit : nullptr;
}
inline void esp_http_client_set_method(void*, int) {}
inline void esp_http_client_set_header(void*, const char*, const char*) {}
inline void esp_http_client_set_post_field(void*, const char*, int) {}
inline int esp_http_client_perform(void*) {
  Fixture::events.push_back("idf.perform");
  if (Fixture::duringRequest) Fixture::duringRequest();
  return Fixture::httpResult > 0 ? ESP_OK : ESP_FAIL;
}
inline int esp_http_client_get_status_code(void*) { return Fixture::httpResult; }
inline int esp_http_client_cleanup(void*) { Fixture::events.push_back("idf.end"); return ESP_OK; }
