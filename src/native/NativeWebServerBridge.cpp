#include <T5AppApi.h>
#include <T5WebServerApi.h>

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HalStorage.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace {
constexpr uint16_t kDnsPort = 53;
constexpr uint16_t kHttpPort = 80;
constexpr size_t kStreamChunk = 1024;

std::unique_ptr<DNSServer> dnsServer;
std::unique_ptr<WebServer> httpServer;
t5_web_server_config_t activeConfig = {};
t5_web_server_state_t currentState = {};
bool mdnsRunning = false;

template <size_t N>
void copyText(char (&dest)[N], const char* source) {
  if (N == 0) return;
  if (!source) source = "";
  size_t i = 0;
  while (i + 1 < N && source[i]) {
    dest[i] = source[i];
    ++i;
  }
  dest[i] = '\0';
}

size_t boundedLength(const char* text, size_t capacity) {
  if (!text) return 0;
  size_t length = 0;
  while (length < capacity && text[length]) ++length;
  return length;
}

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

bool supported() { return true; }

bool validHostname(const char* hostname) {
  const size_t length = boundedLength(hostname, T5_WEB_SERVER_HOSTNAME_MAX);
  if (length == 0 || length >= T5_WEB_SERVER_HOSTNAME_MAX) return false;
  if (!std::isalnum(static_cast<unsigned char>(hostname[0])) ||
      !std::isalnum(static_cast<unsigned char>(hostname[length - 1]))) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(hostname[i]);
    if (!std::isalnum(c) && c != '-') return false;
  }
  return true;
}

bool validConfig(const t5_web_server_config_t* config) {
  if (!config) return false;
  const size_t ssidLength = boundedLength(config->ssid, T5_WEB_SERVER_SSID_MAX);
  const size_t passwordLength = boundedLength(config->password, T5_WEB_SERVER_PASSWORD_MAX);
  const size_t rootLength = boundedLength(config->document_root, T5_WEB_SERVER_ROOT_MAX);
  if (ssidLength == 0 || ssidLength >= T5_WEB_SERVER_SSID_MAX) return false;
  if (passwordLength >= T5_WEB_SERVER_PASSWORD_MAX || (passwordLength != 0 && passwordLength < 8)) return false;
  if (!validHostname(config->hostname)) return false;
  if (rootLength == 0 || rootLength >= T5_WEB_SERVER_ROOT_MAX || config->document_root[0] != '/') return false;
  if (std::strstr(config->document_root, "..") || std::strchr(config->document_root, '\\')) return false;
  if (config->channel < 1 || config->channel > 13 || config->max_connections < 1 || config->max_connections > 8)
    return false;
  return true;
}

void defaultConfig(t5_web_server_config_t* config) {
  if (!config) return;
  std::memset(config, 0, sizeof(*config));
  copyText(config->ssid, "Manifold");
  copyText(config->hostname, "manifold");
  copyText(config->document_root, "/html");
  config->channel = 1;
  config->max_connections = 4;
}

void shutdownNetwork() {
  if (httpServer) {
    httpServer->stop();
    httpServer.reset();
  }
  if (dnsServer) {
    dnsServer->stop();
    dnsServer.reset();
  }
  if (mdnsRunning) {
    MDNS.end();
    mdnsRunning = false;
  }

  const wifi_mode_t mode = WiFi.getMode();
  if (mode & WIFI_MODE_AP) WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void setError(int32_t error) {
  shutdownNetwork();
  currentState.status = T5_WEB_SERVER_STATUS_ERROR;
  currentState.last_error = error;
  currentState.clients = 0;
  currentState.dns_active = 0;
  currentState.mdns_active = 0;
}

String canonicalHost() {
  String host(activeConfig.hostname);
  host += ".local";
  return host;
}

String canonicalUrl() {
  String url("http://");
  url += canonicalHost();
  url += "/";
  return url;
}

bool isCaptiveProbe(const String& uri) {
  return uri == "/generate_204" || uri == "/gen_204" || uri == "/hotspot-detect.html" ||
         uri == "/library/test/success.html" || uri == "/canonical.html" || uri == "/ncsi.txt" ||
         uri == "/connecttest.txt" || uri == "/redirect" || uri == "/fwlink";
}

bool hostIsPortal(const String& host) {
  if (host.isEmpty()) return true;
  const String canonical = canonicalHost();
  if (host.equalsIgnoreCase(canonical)) return true;
  String canonicalWithPort = canonical;
  canonicalWithPort += ":80";
  if (host.equalsIgnoreCase(canonicalWithPort)) return true;
  const String ip(currentState.ip);
  if (host == ip) return true;
  String ipWithPort = ip;
  ipWithPort += ":80";
  return host == ipWithPort;
}

void redirectToPortal() {
  if (!httpServer) return;
  httpServer->sendHeader("Location", canonicalUrl(), true);
  httpServer->sendHeader("Cache-Control", "no-store");
  httpServer->send(302, "text/plain", "");
}

const char* mimeType(const String& path) {
  String lower(path);
  lower.toLowerCase();
  if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html; charset=utf-8";
  if (lower.endsWith(".css")) return "text/css; charset=utf-8";
  if (lower.endsWith(".js") || lower.endsWith(".mjs")) return "text/javascript; charset=utf-8";
  if (lower.endsWith(".json") || lower.endsWith(".map")) return "application/json; charset=utf-8";
  if (lower.endsWith(".txt")) return "text/plain; charset=utf-8";
  if (lower.endsWith(".xml")) return "application/xml; charset=utf-8";
  if (lower.endsWith(".svg")) return "image/svg+xml";
  if (lower.endsWith(".png")) return "image/png";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
  if (lower.endsWith(".gif")) return "image/gif";
  if (lower.endsWith(".webp")) return "image/webp";
  if (lower.endsWith(".ico")) return "image/x-icon";
  if (lower.endsWith(".woff")) return "font/woff";
  if (lower.endsWith(".woff2")) return "font/woff2";
  if (lower.endsWith(".ttf")) return "font/ttf";
  if (lower.endsWith(".wasm")) return "application/wasm";
  if (lower.endsWith(".pdf")) return "application/pdf";
  return "application/octet-stream";
}

bool safeUri(const String& uri) {
  if (uri.isEmpty() || uri[0] != '/' || uri.length() > 240) return false;
  return uri.indexOf("..") < 0 && uri.indexOf('\\') < 0;
}

bool streamFile(String path) {
  if (!httpServer) return false;

  HalFile file;
  if (!Storage.openFileForRead("PORTAL", path.c_str(), file)) return false;
  if (file.isDirectory()) {
    file.close();
    if (!path.endsWith("/")) path += "/";
    path += "index.html";
    if (!Storage.openFileForRead("PORTAL", path.c_str(), file) || file.isDirectory()) return false;
  }

  const size_t total = file.fileSize();
  const char* type = mimeType(path);
  httpServer->sendHeader("X-Content-Type-Options", "nosniff");
  if (path.endsWith(".html") || path.endsWith(".htm"))
    httpServer->sendHeader("Cache-Control", "no-store");
  else
    httpServer->sendHeader("Cache-Control", "public, max-age=3600");
  httpServer->setContentLength(total);
  httpServer->send(200, type, "");

  uint8_t buffer[kStreamChunk];
  uint32_t sent = 0;
  while (file.available()) {
    const int count = file.read(buffer, sizeof(buffer));
    if (count <= 0) break;
    httpServer->sendContent(reinterpret_cast<const char*>(buffer), static_cast<size_t>(count));
    sent += static_cast<uint32_t>(count);
    esp_task_wdt_reset();
    yield();
  }
  file.close();
  currentState.bytes_served += sent;
  currentState.files_served++;
  return sent == total;
}

void handleHttpRequest() {
  if (!httpServer) return;
  currentState.requests++;

  const String uri = httpServer->uri();
  if (isCaptiveProbe(uri) || !hostIsPortal(httpServer->hostHeader())) {
    redirectToPortal();
    return;
  }
  if (httpServer->method() != HTTP_GET) {
    httpServer->sendHeader("Allow", "GET");
    httpServer->send(405, "text/plain; charset=utf-8", "Method not allowed");
    return;
  }
  if (!safeUri(uri)) {
    httpServer->send(400, "text/plain; charset=utf-8", "Invalid path");
    return;
  }

  String root(activeConfig.document_root);
  while (root.length() > 1 && root.endsWith("/")) root.remove(root.length() - 1);
  String path(root);
  if (uri == "/") {
    path += "/index.html";
  } else {
    path += uri;
    if (uri.endsWith("/")) path += "index.html";
  }

  if (streamFile(path)) return;

  // Friendly directory URLs without a trailing slash.
  String indexPath(path);
  indexPath += "/index.html";
  if (streamFile(indexPath)) return;

  httpServer->send(404, "text/plain; charset=utf-8", "Not found");
}

bool start(const t5_web_server_config_t* config) {
  if (!active() || !validConfig(config)) {
    std::memset(&currentState, 0, sizeof(currentState));
    currentState.status = T5_WEB_SERVER_STATUS_ERROR;
    currentState.last_error = T5_WEB_SERVER_ERROR_INVALID_CONFIG;
    return false;
  }

  shutdownNetwork();
  std::memset(&currentState, 0, sizeof(currentState));
  activeConfig = *config;
  currentState.status = T5_WEB_SERVER_STATUS_OFF;
  currentState.channel = activeConfig.channel;
  copyText(currentState.ssid, activeConfig.ssid);
  copyText(currentState.document_root, activeConfig.document_root);
  String host = canonicalHost();
  copyText(currentState.hostname, host.c_str());

  String root(activeConfig.document_root);
  while (root.length() > 1 && root.endsWith("/")) root.remove(root.length() - 1);
  const String indexPath = root + "/index.html";
  if (!Storage.ready() || !Storage.exists(root.c_str()) || !Storage.exists(indexPath.c_str())) {
    setError(T5_WEB_SERVER_ERROR_STORAGE);
    return false;
  }

  WiFi.mode(WIFI_AP);
  delay(50);
  WiFi.setSleep(false);
  const bool openNetwork = activeConfig.password[0] == '\0';
  const bool apStarted = WiFi.softAP(activeConfig.ssid, openNetwork ? nullptr : activeConfig.password,
                                     activeConfig.channel, false, activeConfig.max_connections);
  if (!apStarted) {
    setError(T5_WEB_SERVER_ERROR_WIFI);
    return false;
  }
  delay(50);

  const IPAddress apIp = WiFi.softAPIP();
  snprintf(currentState.ip, sizeof(currentState.ip), "%u.%u.%u.%u", apIp[0], apIp[1], apIp[2], apIp[3]);

  dnsServer.reset(new (std::nothrow) DNSServer());
  if (!dnsServer || !dnsServer->start(kDnsPort, "*", apIp)) {
    setError(T5_WEB_SERVER_ERROR_DNS);
    return false;
  }
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  currentState.dns_active = 1;

  mdnsRunning = MDNS.begin(activeConfig.hostname);
  if (mdnsRunning) {
    MDNS.addService("http", "tcp", kHttpPort);
    currentState.mdns_active = 1;
  }

  httpServer.reset(new (std::nothrow) WebServer(kHttpPort));
  if (!httpServer) {
    setError(T5_WEB_SERVER_ERROR_HTTP);
    return false;
  }
  httpServer->onNotFound(handleHttpRequest);
  httpServer->begin();

  currentState.status = T5_WEB_SERVER_STATUS_RUNNING;
  currentState.last_error = T5_WEB_SERVER_ERROR_NONE;
  currentState.clients = static_cast<uint8_t>(WiFi.softAPgetStationNum());
  return true;
}

void stop() {
  shutdownNetwork();
  currentState.status = T5_WEB_SERVER_STATUS_OFF;
  currentState.last_error = T5_WEB_SERVER_ERROR_NONE;
  currentState.clients = 0;
  currentState.dns_active = 0;
  currentState.mdns_active = 0;
}

bool service() {
  if (!active() || currentState.status != T5_WEB_SERVER_STATUS_RUNNING || !dnsServer || !httpServer) return false;
  dnsServer->processNextRequest();
  httpServer->handleClient();
  currentState.clients = static_cast<uint8_t>(WiFi.softAPgetStationNum());
  esp_task_wdt_reset();
  return true;
}

bool readState(t5_web_server_state_t* state) {
  if (!state) return false;
  if (currentState.status == T5_WEB_SERVER_STATUS_RUNNING)
    currentState.clients = static_cast<uint8_t>(WiFi.softAPgetStationNum());
  *state = currentState;
  return true;
}

const t5_web_server_api_v1 kApi = {
    T5_WEB_SERVER_API_VERSION,
    sizeof(t5_web_server_api_v1),
    supported,
    defaultConfig,
    start,
    stop,
    service,
    readState,
};
}  // namespace

extern "C" const t5_web_server_api_v1* t5_web_server_get_api(uint32_t apiVersion) {
  if (apiVersion != T5_WEB_SERVER_API_VERSION || !active()) return nullptr;
  return &kApi;
}
