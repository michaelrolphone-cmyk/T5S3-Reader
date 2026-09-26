#include <T5AppApi.h>
#include <T5NetworkApi.h>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>

#include <algorithm>
#include <climits>
#include <cstring>

#include "runtime/network/NetworkService.h"
#include "runtime/network/SavedNetworkConnection.h"

namespace {
constexpr uint32_t kMaxHeaders = 16;
constexpr uint32_t kDefaultTimeoutMs = 30000;

struct ResponseSink {
  char* buffer = nullptr;
  size_t capacity = 0;
  size_t copied = 0;
  size_t total = 0;
  bool truncated = false;
};

esp_err_t onHttpEvent(esp_http_client_event_t* event) {
  if (!event || event->event_id != HTTP_EVENT_ON_DATA || !event->user_data || event->data_len <= 0) return ESP_OK;
  auto* sink = static_cast<ResponseSink*>(event->user_data);
  const size_t incoming = static_cast<size_t>(event->data_len);
  sink->total += incoming;

  if (!sink->buffer || sink->capacity == 0) {
    if (incoming != 0) sink->truncated = true;
    return ESP_OK;
  }

  const size_t usable = sink->capacity - 1;
  const size_t room = sink->copied < usable ? usable - sink->copied : 0;
  const size_t take = std::min(room, incoming);
  if (take != 0) {
    std::memcpy(sink->buffer + sink->copied, event->data, take);
    sink->copied += take;
    sink->buffer[sink->copied] = '\0';
  }
  if (take != incoming) sink->truncated = true;
  return ESP_OK;
}

bool wifiConnected() { return RuntimeNetwork::connected(); }

bool httpRequest(const char* url, uint8_t method, const t5_http_header_t* headers, uint32_t headerCount,
                 const void* body, size_t bodySize, const char* certPem, uint32_t timeoutMs, char* response,
                 size_t responseCapacity, t5_http_result_t* result) {
  if (!result) return false;
  *result = {};
  if (!url || !url[0] || headerCount > kMaxHeaders || (headerCount && !headers) ||
      (bodySize && !body) || (responseCapacity && !response) || bodySize > static_cast<size_t>(INT_MAX) ||
      (method != T5_HTTP_METHOD_GET && method != T5_HTTP_METHOD_POST)) {
    return false;
  }
  if (t5_app_get_api(T5_APP_ABI_VERSION) == nullptr) return false;
  if (response && responseCapacity) response[0] = '\0';

  const uint32_t connectBudget = std::min(timeoutMs ? timeoutMs : kDefaultTimeoutMs, 15000u);
  if (!RuntimeNetwork::ensureSavedConnection(connectBudget)) {
    result->transport_error = ESP_ERR_INVALID_STATE;
    return false;
  }

  ResponseSink sink{response, responseCapacity};
  const bool hasCallerCertificate = certPem && certPem[0];
  esp_http_client_config_t config = {};
  config.url = url;
  // Let esp_http_client select plain HTTP vs TLS from the URL. For public
  // HTTPS endpoints, use the firmware's Mozilla-derived root bundle unless
  // the application supplied a service-specific CA certificate.
  config.transport_type = HTTP_TRANSPORT_UNKNOWN;
  config.cert_pem = hasCallerCertificate ? certPem : nullptr;
  if (!hasCallerCertificate && std::strncmp(url, "https://", 8) == 0) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }
  config.timeout_ms = timeoutMs ? static_cast<int>(timeoutMs) : static_cast<int>(kDefaultTimeoutMs);
  config.event_handler = onHttpEvent;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;
  config.user_data = &sink;
  config.skip_cert_common_name_check = false;
  config.keep_alive_enable = false;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    result->transport_error = ESP_ERR_NO_MEM;
    return false;
  }

  esp_http_client_set_method(client, method == T5_HTTP_METHOD_POST ? HTTP_METHOD_POST : HTTP_METHOD_GET);
  for (uint32_t i = 0; i < headerCount; ++i) {
    if (!headers[i].name || !headers[i].name[0] || !headers[i].value) {
      esp_http_client_cleanup(client);
      return false;
    }
    esp_http_client_set_header(client, headers[i].name, headers[i].value);
  }
  if (method == T5_HTTP_METHOD_POST && bodySize != 0) {
    esp_http_client_set_post_field(client, static_cast<const char*>(body), static_cast<int>(bodySize));
  }

  wifi_ps_type_t previousPs = WIFI_PS_MIN_MODEM;
  const bool havePs = esp_wifi_get_ps(&previousPs) == ESP_OK;
  if (havePs) esp_wifi_set_ps(WIFI_PS_NONE);
  esp_task_wdt_reset();
  const esp_err_t err = esp_http_client_perform(client);
  esp_task_wdt_reset();
  if (havePs) esp_wifi_set_ps(previousPs);

  result->transport_error = err;
  result->status_code = esp_http_client_get_status_code(client);
  result->response_bytes = sink.total;
  if (sink.truncated) result->flags |= T5_HTTP_RESPONSE_TRUNCATED;
  if (response && responseCapacity) response[sink.copied] = '\0';
  esp_http_client_cleanup(client);
  return err == ESP_OK;
}

const t5_network_api_v1 kNetworkApi = {
    T5_NETWORK_API_VERSION,
    sizeof(t5_network_api_v1),
    wifiConnected,
    httpRequest,
};
}  // namespace

extern "C" const t5_network_api_v1* t5_network_get_api(uint32_t apiVersion) {
  if (apiVersion != T5_NETWORK_API_VERSION || t5_app_get_api(T5_APP_ABI_VERSION) == nullptr) return nullptr;
  return &kNetworkApi;
}
