#include <T5AppApi.h>
#include <T5OpdsApi.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "OpdsServerStore.h"

namespace {
std::uint32_t store_add_calls = 0;
bool store_load_ok = true;
const t5_app_api_v1* active_app = reinterpret_cast<const t5_app_api_v1*>(1);
}

extern "C" const t5_app_api_v1* t5_app_get_api(std::uint32_t abi_version) {
  return abi_version == T5_APP_ABI_VERSION ? active_app : nullptr;
}

OpdsServerStore OpdsServerStore::instance;

bool OpdsServerStore::loadFromFile() { return store_load_ok; }

bool OpdsServerStore::addServer(const OpdsServer& server) {
  ++store_add_calls;
  servers.push_back(server);
  return true;
}

bool OpdsServerStore::updateServer(std::size_t index, const OpdsServer& server) {
  if (index >= servers.size()) return false;
  servers[index] = server;
  return true;
}

bool OpdsServerStore::removeServer(std::size_t index) {
  if (index >= servers.size()) return false;
  servers.erase(servers.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

const OpdsServer* OpdsServerStore::getServer(std::size_t index) const {
  return index < servers.size() ? &servers[index] : nullptr;
}

int main() {
  const t5_opds_api_v1* api = t5_opds_get_api(T5_OPDS_API_VERSION);
  assert(api != nullptr);
  assert(api->count() == 0);

  t5_opds_server_t draft{};
  std::strcpy(draft.name, "Calibre");
  std::uint32_t new_index = 99;

  // A name-first add, whitespace-only input, placeholders, malformed URLs,
  // and unterminated API strings must fail before the store sees a record.
  assert(!api->add(&draft, &new_index));
  assert(api->count() == 0);
  assert(store_add_calls == 0);

  std::strcpy(draft.url, "   \t\r\n");
  assert(!api->add(&draft, &new_index));
  assert(api->count() == 0);
  assert(store_add_calls == 0);

  std::strcpy(draft.url, "https://");
  assert(!api->add(&draft, &new_index));
  assert(api->count() == 0);
  assert(store_add_calls == 0);

  std::strcpy(draft.url, "http://");
  assert(!api->add(&draft, &new_index));
  assert(api->count() == 0);
  assert(store_add_calls == 0);

  std::strcpy(draft.url, "books.example/opds");
  assert(!api->add(&draft, &new_index));
  assert(api->count() == 0);
  assert(store_add_calls == 0);

  std::strcpy(draft.url, "https:///opds");
  assert(!api->add(&draft, &new_index));
  assert(api->count() == 0);
  assert(store_add_calls == 0);

  std::strcpy(draft.url, "https:// books.example/opds");
  assert(!api->add(&draft, &new_index));
  assert(api->count() == 0);
  assert(store_add_calls == 0);

  std::memset(draft.url, 'x', sizeof(draft.url));
  assert(!api->add(&draft, &new_index));
  assert(api->count() == 0);
  assert(store_add_calls == 0);

  // Retrying with a real endpoint adds exactly one server.
  std::memset(draft.url, 0, sizeof(draft.url));
  assert(api->count() == 0);
  std::strcpy(draft.url, "https://books.example/opds");
  assert(api->add(&draft, &new_index));
  assert(new_index == 0);
  assert(api->count() == 1);
  assert(store_add_calls == 1);
  t5_opds_server_t saved{};
  assert(api->read(0, &saved));
  assert(std::strcmp(saved.url, "https://books.example/opds") == 0);

  // Independently exercise bridge failure propagation with a store fixture
  // that retains its prior snapshot even after returning a load error.
  store_load_ok = false;
  assert(OPDS_STORE.getCount() == 1);
  assert(api->count() == 0);
  assert(!api->read(0, &saved));
  const t5_opds_server_t empty{};
  assert(std::memcmp(&saved, &empty, sizeof(saved)) == 0);
  store_load_ok = true;
  assert(api->count() == 1 && api->read(0, &saved));
  assert(std::strcmp(saved.url, "https://books.example/opds") == 0);
  return 0;
}
