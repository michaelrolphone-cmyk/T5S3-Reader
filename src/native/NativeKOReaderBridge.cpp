#include <T5AppApi.h>
#include <T5KOReaderApi.h>

#include <Arduino.h>
#include <WiFi.h>

#include <cstring>

#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"

namespace {

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

void copyText(char* dst, size_t capacity, const char* src) {
  if (!dst || capacity == 0) return;
  if (!src) src = "";
  std::strncpy(dst, src, capacity - 1);
  dst[capacity - 1] = '\0';
}

bool readSettings(t5_koreader_settings_t* out) {
  if (!active() || !out) return false;
  *out = {};
  copyText(out->username, sizeof(out->username), KOREADER_STORE.getUsername().c_str());
  copyText(out->password, sizeof(out->password), KOREADER_STORE.getPassword().c_str());
  copyText(out->server_url, sizeof(out->server_url), KOREADER_STORE.getServerUrl().c_str());
  out->match_method = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::BINARY
                          ? T5_KOREADER_MATCH_BINARY
                          : T5_KOREADER_MATCH_FILENAME;
  out->has_credentials = KOREADER_STORE.hasCredentials() ? 1u : 0u;
  return true;
}

bool setUsername(const char* username) {
  if (!active() || !username) return false;
  KOREADER_STORE.setCredentials(username, KOREADER_STORE.getPassword());
  return KOREADER_STORE.saveToFile();
}

bool setPassword(const char* password) {
  if (!active() || !password) return false;
  KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), password);
  return KOREADER_STORE.saveToFile();
}

bool setServerUrl(const char* serverUrl) {
  if (!active() || !serverUrl) return false;
  KOREADER_STORE.setServerUrl(serverUrl);
  return KOREADER_STORE.saveToFile();
}

bool setMatchMethod(uint8_t matchMethod) {
  if (!active() || matchMethod > T5_KOREADER_MATCH_BINARY) return false;
  KOREADER_STORE.setMatchMethod(matchMethod == T5_KOREADER_MATCH_BINARY ? DocumentMatchMethod::BINARY
                                                                        : DocumentMatchMethod::FILENAME);
  return KOREADER_STORE.saveToFile();
}

uint8_t mapAuthStatus(KOReaderSyncClient::Error error) {
  switch (error) {
    case KOReaderSyncClient::OK: return T5_KOREADER_AUTH_OK;
    case KOReaderSyncClient::NO_CREDENTIALS: return T5_KOREADER_AUTH_NO_CREDENTIALS;
    case KOReaderSyncClient::NETWORK_ERROR: return T5_KOREADER_AUTH_NETWORK_ERROR;
    case KOReaderSyncClient::AUTH_FAILED: return T5_KOREADER_AUTH_FAILED;
    case KOReaderSyncClient::SERVER_ERROR: return T5_KOREADER_AUTH_SERVER_ERROR;
    case KOReaderSyncClient::JSON_ERROR: return T5_KOREADER_AUTH_JSON_ERROR;
    case KOReaderSyncClient::NOT_FOUND: return T5_KOREADER_AUTH_NOT_FOUND;
    default: return T5_KOREADER_AUTH_UNKNOWN;
  }
}

bool authenticate(t5_koreader_auth_result_t* out) {
  if (!active() || !out) return false;
  *out = {};
  const auto result = KOReaderSyncClient::authenticate();
  out->status = mapAuthStatus(result);
  out->http_status = KOReaderSyncClient::lastHttpCode;
  copyText(out->message, sizeof(out->message), KOReaderSyncClient::errorString(result));
  return true;
}

void endAuthSession() {
  if (!active()) return;
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

const t5_koreader_api_v1 api = {
    T5_KOREADER_API_VERSION,
    sizeof(t5_koreader_api_v1),
    readSettings,
    setUsername,
    setPassword,
    setServerUrl,
    setMatchMethod,
    authenticate,
    endAuthSession,
};

}  // namespace

extern "C" const t5_koreader_api_v1* t5_koreader_get_api(uint32_t version) {
  return version == T5_KOREADER_API_VERSION && active() ? &api : nullptr;
}
