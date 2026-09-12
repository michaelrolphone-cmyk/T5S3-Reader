#include "Llm7Client.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <esp_http_client.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>

#include "LlmTlsCerts.h"

namespace {
constexpr char kChatHost[] = "api.llm7.io";
constexpr char kChatPath[] = "/v1/chat/completions";
constexpr char kChatUrl[] = "https://api.llm7.io/v1/chat/completions";
constexpr char kModel[] = "fast";
constexpr int kMaxTokens = 256;
constexpr size_t kMaxResponseBytes = 8192;

struct ResponseSink {
  std::string body;
};

esp_err_t onHttpEvent(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == nullptr) {
    return ESP_OK;
  }
  auto* sink = static_cast<ResponseSink*>(event->user_data);
  if (sink->body.size() >= kMaxResponseBytes) {
    return ESP_OK;
  }
  const size_t room = kMaxResponseBytes - sink->body.size();
  const size_t take = event->data_len < room ? event->data_len : room;
  sink->body.append(static_cast<const char*>(event->data), take);
  return ESP_OK;
}

esp_err_t performOnce(const std::string& payload, ResponseSink* sink, int* statusOut) {
  // skip_cert_common_name_check also suppresses SNI on this Arduino-ESP32
  // stack. Cloudflare then sends a fatal handshake alert (-0x7780).
  // Leave CN checking on so SNI is api.llm7.io (SAN: *.llm7.io).
  esp_http_client_config_t config = {};
  config.url = kChatUrl;
  config.host = kChatHost;
  config.path = kChatPath;
  config.port = 443;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  config.cert_pem = kLlm7TlsRoots;
  config.timeout_ms = 30000;
  config.event_handler = onHttpEvent;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;
  config.user_data = sink;
  config.skip_cert_common_name_check = false;
  config.keep_alive_enable = false;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return ESP_FAIL;
  }

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "Authorization", "Bearer unused");
  esp_http_client_set_header(client, "User-Agent", "Manifold-ESP32");
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "Host", kChatHost);
  esp_http_client_set_post_field(client, payload.c_str(), static_cast<int>(payload.size()));

  esp_task_wdt_reset();
  const esp_err_t err = esp_http_client_perform(client);
  esp_task_wdt_reset();
  if (statusOut != nullptr) {
    *statusOut = esp_http_client_get_status_code(client);
  }
  esp_http_client_cleanup(client);
  return err;
}
}  // namespace

LlmChatResult Llm7Client::complete(const std::vector<LlmChatMessage>& messages) {
  LlmChatResult result;

  JsonDocument requestDoc;
  requestDoc["model"] = kModel;
  requestDoc["max_tokens"] = kMaxTokens;
  requestDoc["temperature"] = 0.3;
  JsonArray msgs = requestDoc["messages"].to<JsonArray>();
  for (const auto& message : messages) {
    JsonObject item = msgs.add<JsonObject>();
    item["role"] = message.role;
    item["content"] = message.content;
  }

  std::string payload;
  serializeJson(requestDoc, payload);
  if (payload.empty()) {
    result.error = "Could not build request";
    return result;
  }

  ResponseSink sink;
  int status = 0;
  esp_wifi_set_ps(WIFI_PS_NONE);
  LOG_INF("LLM", "POST %s (%u bytes)", kChatUrl, static_cast<unsigned>(payload.size()));
  esp_err_t err = performOnce(payload, &sink, &status);
  if (err == ESP_ERR_HTTP_CONNECT) {
    LOG_ERR("LLM", "connect failed, retrying once");
    delay(500);
    sink.body.clear();
    err = performOnce(payload, &sink, &status);
  }
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (err != ESP_OK) {
    LOG_ERR("LLM", "perform failed: %s", esp_err_to_name(err));
    result.error = esp_err_to_name(err);
    return result;
  }

  if (status < 200 || status >= 300) {
    LOG_ERR("LLM", "HTTP %d body=%s", status, sink.body.c_str());
    result.error = "HTTP " + std::to_string(status);
    return result;
  }

  JsonDocument responseDoc;
  const DeserializationError parseErr = deserializeJson(responseDoc, sink.body);
  if (parseErr) {
    result.error = "Bad JSON from LLM7";
    return result;
  }

  const char* content = responseDoc["choices"][0]["message"]["content"];
  if (content == nullptr || content[0] == '\0') {
    const char* apiErr = responseDoc["error"]["message"];
    result.error = apiErr ? apiErr : "Empty reply";
    return result;
  }

  result.ok = true;
  result.text = content;
  while (!result.text.empty() && (result.text.back() == '\n' || result.text.back() == '\r')) {
    result.text.pop_back();
  }
  return result;
}
