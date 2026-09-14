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

OpdsServer fromNative(const t5_opds_server_t& server) {
  return OpdsServer{server.name, server.url, server.username, server.password};
}

uint32_t countServers() {
  if (!active()) return 0;
  OPDS_STORE.loadFromFile();
  return static_cast<uint32_t>(OPDS_STORE.getCount());
}

bool readServer(uint32_t index, t5_opds_server_t* out) {
  if (!active() || !out) return false;
  OPDS_STORE.loadFromFile();
  const auto* server = OPDS_STORE.getServer(index);
  if (!server) return false;
  *out = {};
  copyText(out->name, sizeof(out->name), server->name);
  copyText(out->url, sizeof(out->url), server->url);
  copyText(out->username, sizeof(out->username), server->username);
  copyText(out->password, sizeof(out->password), server->password);
  return true;
}

bool addServer(const t5_opds_server_t* server, uint32_t* newIndex) {
  if (!active() || !server || OPDS_STORE.getCount() >= T5_OPDS_MAX_SERVERS) return false;
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
