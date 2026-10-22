#include "native/NativeNetworkBridge.h"
#include "runtime/network/InstalledNetworkLink.h"
#include "runtime/network/NetworkService.h"
#include <T5NetworkApi.h>
#include "NetworkTestSupport.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace Fixture {
std::vector<std::string> events;
bool appActive = true, httpBegin = true, httpInit = true, savedConnection = true;
int httpResult = 200, powerReads = 0, powerWrites = 0, reconnects = 0;
void (*duringRequest)() = nullptr;
bool acquireOkay = true, grantOnFailure = false, releaseOkay = true, recoverOkay = true;
int begins = 0, ends = 0, releases = 0, recoveries = 0, acquisitions = 0;
int beginResult = RISC_NETWORK_LINK_READY, endResult = RISC_NETWORK_LINK_READY;
bool beginToken = true, connected = true, address = true, badState = false;
uint64_t liveToken = 0, nextToken = 0;
uint32_t generation = 0;
RuntimeProviders::GrantV2 liveGrant{};
int32_t begin(void*, uint32_t flags, uint64_t* token) {
  events.push_back("link.begin"); ++begins;
  assert(!liveToken && token && flags == RISC_NETWORK_LINK_LOW_LATENCY);
  *token = beginToken ? ++nextToken : 0;
  liveToken = *token;
  return beginResult;
}
bool state(void*, uint64_t token, risc_network_link_state_v1* out) {
  assert(token == 0 || token == liveToken);
  assert(out && out->struct_size == sizeof(*out));
  out->connected = connected; out->has_address = address;
  if (badState) std::memset(out->address, 'x', sizeof(out->address));
  else std::strcpy(out->address, "192.0.2.1");
  return true;
}
int32_t end(void*, uint64_t token) {
  events.push_back("link.end"); ++ends;
  assert(token && token == liveToken);
  if (endResult == RISC_NETWORK_LINK_READY) liveToken = 0;
  return endResult;
}
risc_network_link_v1 api{RISC_NETWORK_LINK_API_V1, sizeof(api), nullptr, begin, state, end};
void reset() {
  assert(liveToken == 0 && liveGrant.slot == 0);
  events.clear();
  appActive = httpBegin = httpInit = savedConnection = true;
  httpResult = 200; powerReads = powerWrites = reconnects = 0; duringRequest = nullptr;
  acquireOkay = releaseOkay = recoverOkay = true; grantOnFailure = false;
  begins = ends = releases = recoveries = acquisitions = 0;
  beginResult = endResult = RISC_NETWORK_LINK_READY;
  beginToken = connected = address = true; badState = false;
  api = {RISC_NETWORK_LINK_API_V1, sizeof(api), nullptr, begin, state, end};
}
}

namespace RuntimeInstalledProviders {
bool acquire(const char* id, const char* capability, uint32_t version, Lease* out) {
  using namespace Fixture;
  assert(std::strcmp(id, "test.network") == 0);
  assert(std::strcmp(capability, RISC_NETWORK_LINK_CAPABILITY) == 0);
  assert(version == RISC_NETWORK_LINK_API_V1 && !liveGrant.slot);
  events.push_back("graph.acquire"); ++acquisitions;
  *out = {};
  if (acquireOkay || grantOnFailure) {
    liveGrant = {1, ++generation}; out->grant = liveGrant;
    out->interface = acquireOkay ? &api : nullptr;
  }
  return acquireOkay;
}
bool release(Lease* lease) {
  using namespace Fixture;
  assert(!liveToken && lease && lease->grant.slot == liveGrant.slot &&
         lease->grant.generation == liveGrant.generation);
  events.push_back("graph.release"); ++releases;
  if (!releaseOkay) return false;
  liveGrant = {}; *lease = {}; return true;
}
bool recoverFailedProvider(const char* id, const char* capability, uint32_t version) {
  using namespace Fixture;
  assert(!liveGrant.slot && std::strcmp(id, "test.network") == 0);
  assert(std::strcmp(capability, RISC_NETWORK_LINK_CAPABILITY) == 0 && version == 1);
  ++recoveries; return recoverOkay;
}
}

namespace RuntimeNetwork {
bool connected() { return Fixture::connected; }
bool ensureSavedConnection(uint32_t) { ++Fixture::reconnects; return Fixture::savedConnection; }
}

bool request(const char* url = "https://example.test/") {
  char response[8]{};
  t5_http_result_t result{};
  const auto* api = t5_network_get_api(1);
  assert(api && api->struct_size == sizeof(*api));
  return api->http_request(url, T5_HTTP_METHOD_GET, nullptr, 0, nullptr, 0,
                           nullptr, 1000, response, sizeof(response), &result);
}
void finish() {
  nativeNetworkEnd();
  assert(nativeNetworkReleaseInstalledLink());
  assert(!Fixture::liveGrant.slot && !Fixture::liveToken);
}

int main() {
  using namespace Fixture;
  // ABI rejects before invoking any provider callback, preserving the grant
  // for generic checked teardown rather than borrowing an invalid table.
  for (int invalid = 0; invalid != 5; ++invalid) {
    reset();
    if (invalid == 0) api.api_version = 2;
    if (invalid == 1) api.struct_size = RISC_NETWORK_LINK_V1_SIZE - 1;
    if (invalid == 2) api.begin_use = nullptr;
    if (invalid == 3) api.state = nullptr;
    if (invalid == 4) api.end_use = nullptr;
    assert(!nativeNetworkBindInstalledLink("test.network"));
    assert(!request() && begins == 0 && powerReads == 0 && reconnects == 0);
    assert(!nativeNetworkBindInstalledLink("test.network") && acquisitions == 1);
    finish(); assert(releases == 1);
  }
  reset(); api.struct_size += 16; // Compatible append-only extension.
  assert(nativeNetworkBindInstalledLink("test.network"));
  nativeNetworkBegin();
  assert(t5_network_get_api(1)->wifi_connected());
  duringRequest = [] {
    assert(!nativeNetworkReleaseInstalledLink());
    assert(!nativeNetworkBindInstalledLink("test.network"));
    assert(!request());
  };
  assert(request());
  assert((events == std::vector<std::string>{"graph.acquire", "link.begin", "http.begin",
                                           "http.perform", "http.end", "link.end"}));
  assert(powerReads == 0 && powerWrites == 0 && reconnects == 0);
  duringRequest = nullptr;
  assert(request("http://example.test/"));
  assert(events[events.size()-2] == "idf.end" && events.back() == "link.end");
  finish();

  // Malformed and partial begin never lose activity custody or retry begin.
  for (int mode = 0; mode != 3; ++mode) {
    reset();
    beginResult = mode == 0 ? RISC_NETWORK_LINK_READY : RISC_NETWORK_LINK_REJECTED;
    beginToken = mode == 2;
    assert(nativeNetworkBindInstalledLink("test.network"));
    nativeNetworkBegin();
    assert(!request());
    assert(!request() && begins == 1 && reconnects == 0 && powerReads == 0);
    finish();
    assert(ends == (mode == 2 ? 1 : 0) && releases == 1);
  }

  // Pending/failed/unknown end preserves both exact token and graph generation
  // across the legacy void End, next Begin and explicit cleanup retries.
  for (int status : {RISC_NETWORK_LINK_PENDING, RISC_NETWORK_LINK_RETAINED, -19}) {
    reset(); endResult = status;
    assert(nativeNetworkBindInstalledLink("test.network"));
    nativeNetworkBegin(); assert(!request());
    const uint64_t token = liveToken;
    const uint32_t grant = liveGrant.generation;
    assert(token && grant && releases == 0);
    nativeNetworkEnd();
    nativeNetworkBegin();
    assert(!request() && begins == 1 && liveToken == token && liveGrant.generation == grant);
    assert(!nativeNetworkReleaseInstalledLink() && releases == 0);
    endResult = RISC_NETWORK_LINK_READY;
    assert(nativeNetworkReleaseInstalledLink());
    finish();
  }

  reset(); releaseOkay = false;
  assert(nativeNetworkBindInstalledLink("test.network"));
  nativeNetworkBegin(); assert(request());
  const int priorEnds = ends;
  nativeNetworkEnd();
  assert(liveGrant.slot && !liveToken);
  assert(!request() && !nativeNetworkReleaseInstalledLink() && ends == priorEnds);
  releaseOkay = true; finish(); assert(ends == priorEnds);

  for (bool retainedGrant : {false, true}) {
    reset(); acquireOkay = false; grantOnFailure = retainedGrant;
    recoverOkay = releaseOkay = false;
    assert(!nativeNetworkBindInstalledLink("test.network"));
    nativeNetworkEnd();
    assert(!request() && reconnects == 0 && acquisitions == 1);
    assert(!nativeNetworkReleaseInstalledLink());
    recoverOkay = releaseOkay = true; finish();
    assert(retainedGrant ? releases >= 2 : recoveries >= 2);
  }

  // Every synchronous consumer return relinquishes activity. Invalid copied
  // state and HTTP setup/error paths must not fall through to legacy Wi-Fi.
  for (int failure = 0; failure != 6; ++failure) {
    reset();
    if (failure == 0) connected = false;
    if (failure == 1) address = false;
    if (failure == 2) badState = true;
    if (failure == 3) httpBegin = false;
    if (failure == 4) httpResult = -1;
    if (failure == 5) httpInit = false;
    assert(nativeNetworkBindInstalledLink("test.network"));
    nativeNetworkBegin();
    assert(!request(failure == 5 ? "http://example.test/" : "https://example.test/"));
    assert(ends == 1 && !liveToken && reconnects == 0 && powerReads == 0);
    finish();
  }
  reset(); nativeNetworkBegin();
  assert(!nativeNetworkBindInstalledLink("test.network"));
  assert(request() && reconnects == 1 && powerReads == 1 && powerWrites == 2);
  assert(request("http://example.test/") && reconnects == 2 && powerReads == 2 && powerWrites == 4);
  finish();
  puts("Installed network link: production HTTP custody, ABI gates, partial admission, retained exit/retry and unchanged legacy power PASS");
}
