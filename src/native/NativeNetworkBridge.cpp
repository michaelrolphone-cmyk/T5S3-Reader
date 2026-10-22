#include <T5AppApi.h>
#include <T5NetworkApi.h>
#include "NativeNetworkBridge.h"

#include <HTTPClient.h>
#if __has_include(<NetworkClientSecure.h>)
#include <NetworkClientSecure.h>
using RiscRteSecureClient = NetworkClientSecure;
#else
#include <WiFiClientSecure.h>
using RiscRteSecureClient = WiFiClientSecure;
#endif
#include <esp_http_client.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <algorithm>
#include <climits>
#include <cstring>

#include "runtime/network/NetworkService.h"
#include "runtime/network/SavedNetworkConnection.h"
#include "runtime/network/InstalledNetworkLink.h"

namespace {
constexpr uint32_t kMaxHeaders = 16;
constexpr uint32_t kDefaultTimeoutMs = 30000;
constexpr size_t kMaxSessionCookies = 8;
constexpr size_t kMaxCookieNameBytes = 64;
constexpr size_t kMaxCookieValueBytes = 256;
constexpr size_t kMaxCookieDomainBytes = 128;
constexpr size_t kMaxCookiePathBytes = 128;

CookieJar nativeCookieJar;
bool nativeCookieSessionActive = false;
// Survives application return and failed cleanup. Never an app-stack RAII
// object whose destructor could discard a pending activity/provider grant.
RuntimeNetwork::InstalledLink installedLink;
bool requestActive = false;
bool installedSession = false;
struct RequestGuard {
  RequestGuard() { requestActive = true; }
  ~RequestGuard() { requestActive = false; }
};

// Binding TCP/TLS sockets does not bind the host DNS resolver. Limit the new
// installed path to numeric destinations until a provider-scoped resolver is
// available, and disable redirects so a response cannot introduce a hostname.
bool numericDestination(const char* url) {
  if (!url) return false;
  const char* host = nullptr;
  if (std::strncmp(url, "http://", 7) == 0) host = url + 7;
  else if (std::strncmp(url, "https://", 8) == 0) host = url + 8;
  else return false;
  char address[INET6_ADDRSTRLEN]{};
  const bool ipv6 = *host == '[';
  if (ipv6) ++host;
  const char* end = host;
  while (*end && (ipv6 ? *end != ']' : *end != ':' && *end != '/' && *end != '?' && *end != '#')) ++end;
  const size_t length = static_cast<size_t>(end - host);
  if (!length || length >= sizeof(address) || (ipv6 && *end != ']')) return false;
  std::memcpy(address, host, length);
  unsigned char parsed[sizeof(in6_addr)]{};
  if (inet_pton(ipv6 ? AF_INET6 : AF_INET, address, parsed) != 1) return false;
  if (ipv6) ++end;
  if (*end == ':') {
    ++end;
    if (*end < '0' || *end > '9') return false;
    unsigned port = 0;
    while (*end >= '0' && *end <= '9') {
      port = port * 10 + static_cast<unsigned>(*end++ - '0');
      if (port > 65535) return false;
    }
    if (!port) return false;
  }
  return !*end || *end == '/' || *end == '?' || *end == '#';
}

void clearNativeCookieJar() {
  CookieJar empty;
  nativeCookieJar.swap(empty);
}

void boundNativeCookieJar() {
  for (auto it = nativeCookieJar.begin(); it != nativeCookieJar.end();) {
    if (it->name.length() > kMaxCookieNameBytes ||
        it->value.length() > kMaxCookieValueBytes ||
        it->domain.length() > kMaxCookieDomainBytes ||
        it->path.length() > kMaxCookiePathBytes) {
      it = nativeCookieJar.erase(it);
    } else {
      ++it;
    }
  }
  if (nativeCookieJar.size() > kMaxSessionCookies) {
    nativeCookieJar.erase(nativeCookieJar.begin(),
                          nativeCookieJar.begin() + (nativeCookieJar.size() - kMaxSessionCookies));
  }
}

struct ResponseSink {
  char* buffer = nullptr;
  size_t capacity = 0;
  size_t copied = 0;
  size_t total = 0;
  bool truncated = false;
};

size_t appendResponse(ResponseSink& sink, const uint8_t* data, size_t incoming) {
  sink.total += incoming;
  if (!data || incoming == 0) return incoming;
  if (!sink.buffer || sink.capacity == 0) {
    sink.truncated = true;
    return incoming;
  }
  const size_t usable = sink.capacity - 1;
  const size_t room = sink.copied < usable ? usable - sink.copied : 0;
  const size_t take = std::min(room, incoming);
  if (take != 0) {
    std::memcpy(sink.buffer + sink.copied, data, take);
    sink.copied += take;
    sink.buffer[sink.copied] = '\0';
  }
  if (take != incoming) sink.truncated = true;
  return incoming;
}

class ResponseStream final : public Stream {
 public:
  explicit ResponseStream(ResponseSink& sink) : sink_(sink) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    return appendResponse(sink_, data, size);
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  ResponseSink& sink_;
};

esp_err_t onHttpEvent(esp_http_client_event_t* event) {
  if (!event || event->event_id != HTTP_EVENT_ON_DATA || !event->user_data || event->data_len <= 0) return ESP_OK;
  auto* sink = static_cast<ResponseSink*>(event->user_data);
  appendResponse(*sink, static_cast<const uint8_t*>(event->data),
                 static_cast<size_t>(event->data_len));
  return ESP_OK;
}

bool performInsecureHttps(const char* url, uint8_t method,
                          const t5_http_header_t* headers, uint32_t headerCount,
                          const void* body, size_t bodySize, uint32_t timeoutMs,
                          ResponseSink& sink, t5_http_result_t* result, bool providerOwned) {
  RiscRteSecureClient secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  if (nativeCookieSessionActive) {
    if (!nativeCookieJar.empty()) result->flags |= T5_HTTP_SESSION_COOKIES_AVAILABLE;
    http.setCookieJar(&nativeCookieJar);
  }
  if (!http.begin(secureClient, url)) {
    result->transport_error = ESP_FAIL;
    return false;
  }
  const uint32_t requestedTimeout = timeoutMs ? timeoutMs : kDefaultTimeoutMs;
  http.setTimeout(static_cast<uint16_t>(std::min(requestedTimeout, 60000u)));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  for (uint32_t i = 0; i < headerCount; ++i) {
    if (!headers[i].name || !headers[i].name[0] || !headers[i].value) {
      http.end();
      return false;
    }
    http.addHeader(headers[i].name, headers[i].value);
  }

  wifi_ps_type_t previousPs = WIFI_PS_MIN_MODEM;
  const bool havePs = !providerOwned && esp_wifi_get_ps(&previousPs) == ESP_OK;
  if (havePs) esp_wifi_set_ps(WIFI_PS_NONE);
  esp_task_wdt_reset();
  int status = 0;
  if (method == T5_HTTP_METHOD_POST) {
    status = http.sendRequest("POST", const_cast<uint8_t*>(static_cast<const uint8_t*>(body)), bodySize);
  } else {
    status = http.GET();
  }
  int streamed = 0;
  if (status > 0) {
    ResponseStream output(sink);
    streamed = http.writeToStream(&output);
  }
  esp_task_wdt_reset();
  if (havePs) esp_wifi_set_ps(previousPs);

  result->status_code = status > 0 ? status : 0;
  result->response_bytes = sink.total;
  if (sink.truncated) result->flags |= T5_HTTP_RESPONSE_TRUNCATED;
  if (nativeCookieSessionActive) {
    boundNativeCookieJar();
    if (!nativeCookieJar.empty()) result->flags |= T5_HTTP_SESSION_COOKIES_STORED;
  }
  http.end();
  if (status <= 0 || streamed < 0) {
    result->transport_error = ESP_FAIL;
    return false;
  }
  result->transport_error = ESP_OK;
  return true;
}

bool wifiConnected() {
  if (requestActive) return false;
  if (!installedSession && !installedLink.hasCustody()) return RuntimeNetwork::connected();
  RequestGuard guard;
  risc_network_link_state_v1 state{};
  return installedLink.state(&state) && state.connected && state.has_address;
}

bool httpRequestBody(const char* url, uint8_t method, const t5_http_header_t* headers, uint32_t headerCount,
                 const void* body, size_t bodySize, const char* certPem, uint32_t timeoutMs, char* response,
                 size_t responseCapacity, t5_http_result_t* result, const char* interfaceName) {
  const bool providerOwned = interfaceName != nullptr;
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
  if (!providerOwned && !RuntimeNetwork::ensureSavedConnection(connectBudget)) {
    result->transport_error = ESP_ERR_INVALID_STATE;
    return false;
  }

  ResponseSink sink{response, responseCapacity};
  const bool hasCallerCertificate = certPem && certPem[0];
  // Compatibility policy: an HTTPS request with no caller certificate is an
  // explicit insecure request. This matches the existing package/download
  // transport's certificate-bypass behavior and avoids forcing the root bundle
  // (and its handshake memory cost) onto callers that intentionally omit a CA.
  if (!hasCallerCertificate && std::strncmp(url, "https://", 8) == 0) {
    return performInsecureHttps(url, method, headers, headerCount, body, bodySize,
                                timeoutMs, sink, result, providerOwned);
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.transport_type = HTTP_TRANSPORT_UNKNOWN;
  config.cert_pem = hasCallerCertificate ? certPem : nullptr;
  config.timeout_ms = timeoutMs ? static_cast<int>(timeoutMs) : static_cast<int>(kDefaultTimeoutMs);
  config.event_handler = onHttpEvent;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;
  config.user_data = &sink;
  config.skip_cert_common_name_check = false;
  config.keep_alive_enable = false;
  struct ifreq interface{};
  if (providerOwned) {
    const size_t length = std::strlen(interfaceName);
    if (!length || length >= sizeof(interface.ifr_name)) {
      result->transport_error = ESP_ERR_INVALID_STATE;
      return false;
    }
    std::memcpy(interface.ifr_name, interfaceName, length + 1);
    config.if_name = &interface;
    config.disable_auto_redirect = true;
  }

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
  const bool havePs = !providerOwned && esp_wifi_get_ps(&previousPs) == ESP_OK;
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

bool httpRequest(const char* url, uint8_t method, const t5_http_header_t* headers, uint32_t headerCount,
                 const void* body, size_t bodySize, const char* certPem, uint32_t timeoutMs, char* response,
                 size_t responseCapacity, t5_http_result_t* result) {
  if (!result) return false;
  *result = {};
  if (requestActive || t5_app_get_api(T5_APP_ABI_VERSION) == nullptr) return false;
  RequestGuard guard; // Includes provider begin/state/end, not only HTTP I/O.
  const bool providerOwned = installedSession || installedLink.hasCustody();
  risc_network_link_state_v1 state{};
  if (providerOwned) {
    // The Arduino insecure-HTTPS client has no supported pre-connect interface
    // binding. Do not silently route it through the default host interface.
    if (!numericDestination(url) ||
        ((!certPem || !certPem[0]) && std::strncmp(url, "https://", 8) == 0) ||
        !installedLink.beginUse(RISC_NETWORK_LINK_LOW_LATENCY)) {
      result->transport_error = ESP_ERR_INVALID_STATE;
      return false;
    }
    if (!installedLink.state(&state) || !state.connected || !state.has_address ||
        !state.interface_name[0]) {
      (void)installedLink.endUseChecked();
      result->transport_error = ESP_ERR_INVALID_STATE;
      return false;
    }
  }
  const bool completed = httpRequestBody(url, method, headers, headerCount, body, bodySize,
                                        certPem, timeoutMs, response, responseCapacity, result,
                                        providerOwned ? state.interface_name : nullptr);
  // The synchronous HTTP body has ended its client before relinquishing use.
  // Failed link cleanup remains reachable through the explicit release hook.
  if (providerOwned && !installedLink.endUseChecked()) {
    result->transport_error = ESP_ERR_INVALID_STATE;
    return false;
  }
  return completed;
}

const t5_network_api_v1 kNetworkApi = {
    T5_NETWORK_API_VERSION,
    sizeof(t5_network_api_v1),
    wifiConnected,
    httpRequest,
};
}  // namespace

void nativeNetworkBegin() {
  if (requestActive) return;
  clearNativeCookieJar();
  if (!nativeCookieSessionActive) installedSession = installedLink.hasCustody();
  nativeCookieSessionActive = true;
}

void nativeNetworkEnd() {
  if (requestActive) return;
  nativeCookieSessionActive = false;
  installedSession = false;
  clearNativeCookieJar();
  (void)nativeNetworkReleaseInstalledLink();
}

bool nativeNetworkBindInstalledLink(const char* providerId) {
  if (nativeCookieSessionActive || requestActive) return false;
  RequestGuard guard;
  return installedLink.bind(providerId);
}

bool nativeNetworkReleaseInstalledLink() {
  if (requestActive) return false;
  RequestGuard guard;
  return installedLink.releaseChecked();
}

extern "C" const t5_network_api_v1* t5_network_get_api(uint32_t apiVersion) {
  if (apiVersion != T5_NETWORK_API_VERSION || t5_app_get_api(T5_APP_ABI_VERSION) == nullptr) return nullptr;
  return &kNetworkApi;
}
