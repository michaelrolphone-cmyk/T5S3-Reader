#include <T5AppApi.h>
#include <T5OpdsApi.h>

#include <cstring>

#include "OpdsServerStore.h"

namespace {

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

void copyText(char* dst, size_t capacity, const std::string& src) {
  if (!dst || capacity == 0) return;
  std::strncpy(dst, src.c_str(), capacity - 1);
  dst[capacity - 1] = '\0';
}

unsigned char asciiLower(unsigned char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

bool asciiSpace(unsigned char value) {
  return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

bool hasHttpScheme(const char* value, size_t length, const char* scheme, size_t schemeLength) {
  if (length < schemeLength) return false;
  for (size_t i = 0; i < schemeLength; ++i) {
    if (asciiLower(static_cast<unsigned char>(value[i])) != static_cast<unsigned char>(scheme[i])) return false;
  }
  return true;
}

bool hasUsableUrl(const t5_opds_server_t& server) {
  size_t length = 0;
  while (length < sizeof(server.url) && server.url[length] != '\0') ++length;
  if (length == sizeof(server.url)) return false;

  size_t first = 0;
  while (first < length && asciiSpace(static_cast<unsigned char>(server.url[first]))) ++first;
  while (length > first && asciiSpace(static_cast<unsigned char>(server.url[length - 1]))) --length;
  if (first == length) return false;

  const char* url = server.url + first;
  const size_t urlLength = length - first;
  size_t authorityStart = 0;
  if (hasHttpScheme(url, urlLength, "http://", 7)) authorityStart = 7;
  else if (hasHttpScheme(url, urlLength, "https://", 8)) authorityStart = 8;
  else return false;

  size_t authorityEnd = authorityStart;
  while (authorityEnd < urlLength && url[authorityEnd] != '/' && url[authorityEnd] != '?' &&
         url[authorityEnd] != '#') {
    if (asciiSpace(static_cast<unsigned char>(url[authorityEnd]))) return false;
    ++authorityEnd;
  }
  return authorityEnd > authorityStart;
}

OpdsServer fromNative(const t5_opds_server_t& server) {
  return OpdsServer{server.name, server.url, server.username, server.password};
}

uint32_t countServers() {
  if (!active()) return 0;
  if (!OPDS_STORE.loadFromFile()) return 0;
  return static_cast<uint32_t>(OPDS_STORE.getCount());
}

bool readServer(uint32_t index, t5_opds_server_t* out) {
  if (!active() || !out) return false;
  *out = {};
  if (!OPDS_STORE.loadFromFile()) return false;
  const auto* server = OPDS_STORE.getServer(index);
  if (!server) return false;
  copyText(out->name, sizeof(out->name), server->name);
  copyText(out->url, sizeof(out->url), server->url);
  copyText(out->username, sizeof(out->username), server->username);
  copyText(out->password, sizeof(out->password), server->password);
  return true;
}

bool addServer(const t5_opds_server_t* server, uint32_t* newIndex) {
  if (!active() || !server || OPDS_STORE.getCount() >= T5_OPDS_MAX_SERVERS) return false;
  if (!hasUsableUrl(*server)) return false;
  if (!OPDS_STORE.addServer(fromNative(*server))) return false;
  if (newIndex) *newIndex = static_cast<uint32_t>(OPDS_STORE.getCount() - 1);
  return true;
}

bool updateServer(uint32_t index, const t5_opds_server_t* server) {
  if (!active() || !server) return false;
  return OPDS_STORE.updateServer(index, fromNative(*server));
}

bool removeServer(uint32_t index) {
  if (!active()) return false;
  return OPDS_STORE.removeServer(index);
}

const t5_opds_api_v1 api = {
    T5_OPDS_API_VERSION,
    sizeof(t5_opds_api_v1),
    countServers,
    readServer,
    addServer,
    updateServer,
    removeServer,
};

}  // namespace

extern "C" const t5_opds_api_v1* t5_opds_get_api(uint32_t version) {
  return version == T5_OPDS_API_VERSION && active() ? &api : nullptr;
}
