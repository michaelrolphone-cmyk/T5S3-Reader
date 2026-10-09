#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

// The complete production consumer is compiled below. Only the clock, RTOS
// scheduling and installed provider boundary are replaced by deterministic
// fakes; lifecycle operations and their ordering are the real implementation.
using TaskHandle_t = void*;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
static uint32_t nowMs = 1000;
static uint32_t millis() { return nowMs; }
static void delay(uint32_t ms) { nowMs += ms; }
static bool createOkay = true, stopOkay = true;
static unsigned starts;
static void (*workerEntry)(void*);
static int xTaskCreate(void (*entry)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t* out) {
  ++starts;
  if (!createOkay) return 0;
  workerEntry = entry;
  *out = reinterpret_cast<void*>(1);
  return pdPASS;
}
static unsigned ulTaskNotifyTake(int, unsigned) { return 0; }
static void xTaskNotifyGive(TaskHandle_t) { if (stopOkay) workerEntry(nullptr); }
static void vTaskDelete(void*) {}

#include "../../src/native/NativeTouchInput.cpp"

static bool releaseOkay = true, unsubscribeOkay = true, subscribeOkay = true;
static bool partialAcquire, invalidApi, revoked, sharedGrant;
static unsigned acquisitions, releases, unsubscribes, generation, snapshots;
static uint64_t nextSubscription, liveSubscription;
static uint64_t fakeSubscribe(void*) {
  assert(!revoked && !liveSubscription);
  if (!subscribeOkay) return 0;
  return liveSubscription = ++nextSubscription;
}
static bool fakeUnsubscribe(void*, uint64_t token) {
  assert(!revoked && !workerTask && token == liveSubscription);
  ++unsubscribes;
  if (!unsubscribeOkay) return false;
  liveSubscription = 0;
  return true;
}
static bool fakePoll(void*, size_t) { assert(!revoked); return true; }
static int32_t fakeNext(void*, uint64_t, risc_touch_event_v1*) { assert(!revoked); return 0; }
static bool fakeSnapshot(void*, risc_touch_snapshot_v1* out) {
  assert(!revoked); ++snapshots; *out = {}; return true;
}
static const risc_touch_api_v1 provider{
  1, sizeof(provider), nullptr, fakeSubscribe, fakeUnsubscribe, fakePoll, fakeNext, fakeSnapshot};
static const risc_touch_api_v1 invalidProvider{};
namespace RuntimeInstalledProviders {
bool acquireCapability(const char* capability, uint32_t version, Lease* out) {
  assert(!std::strcmp(capability, "input.touch.raw") && version == 1);
  assert(!out->grant.slot && !liveSubscription);
  ++acquisitions;
  revoked = partialAcquire;
  *out = {{1, ++generation}, partialAcquire ? nullptr : (invalidApi ? &invalidProvider : &provider)};
  return !partialAcquire;
}
bool release(Lease* out) {
  assert(!workerTask && !liveSubscription);
  assert(out->grant.slot == 1 && out->grant.generation == generation);
  ++releases;
  revoked = true;
  if (!releaseOkay) return false;
  *out = {};
  return true;
}
bool hasLiveGrants() { return sharedGrant; }
const char* lastError() { return "fixture failure"; }
}

int main() {
  assert(nativeTouchResume() && nativeTouchAvailable());
  assert(nativeTouchSuspend() && !lease.grant.slot && !nativeTouchAvailable());
  const unsigned firstAcquisitions = acquisitions;
  nativeTouchTick(); assert(acquisitions == firstAcquisitions); // suspended means stopped
  assert(nativeTouchResume());

  // A busy worker cannot be unsubscribed, released, or silently restarted.
  stopOkay = false;
  const auto runningGrant = lease.grant;
  const unsigned beforeStop = starts, beforeUnsubscribe = unsubscribes, beforeRelease = releases;
  const uint32_t stopStarted = nowMs;
  assert(!nativeTouchSuspend() && nowMs - stopStarted == kWorkerStopTimeoutMs);
  nativeTouchTick(); assert(!nativeTouchResume() && !nativeTouchAvailable());
  assert(starts == beforeStop && unsubscribes == beforeUnsubscribe && releases == beforeRelease);
  assert(lease.grant.generation == runningGrant.generation && workerTask && subscription);
  stopOkay = true;
  assert(nativeTouchResume() && lease.grant.generation != runningGrant.generation);

  // Failed unsubscribe retains both exact subscription and its still-live API.
  unsubscribeOkay = false;
  const uint64_t pendingSubscription = subscription;
  const auto subscribedGrant = lease.grant;
  const unsigned beforeUnsubscribeRelease = releases;
  assert(!nativeTouchSuspend() && !workerTask);
  assert(!nativeTouchResume()); nativeTouchTick();
  assert(subscription == pendingSubscription && lease.grant.generation == subscribedGrant.generation);
  assert(api == &provider && releases == beforeUnsubscribeRelease);
  unsubscribeOkay = true;
  assert(nativeTouchResume() && subscription != pendingSubscription);

  // Release revokes API access even while its exact grant remains pinned.
  releaseOkay = false;
  assert(!nativeTouchSuspend() && !api && !subscription);
  const auto pendingGrant = lease.grant;
  const unsigned beforeRevokedRetry = acquisitions, beforeRevokedSnapshot = snapshots;
  assert(!nativeTouchResume()); nativeTouchTick();
  assert(lease.grant.generation == pendingGrant.generation && acquisitions == beforeRevokedRetry);
  assert(snapshots == beforeRevokedSnapshot);
  releaseOkay = true;
  assert(nativeTouchResume() && lease.grant.generation != pendingGrant.generation);
  assert(nativeTouchSuspend());

  // Failed/invalid acquisition, subscription failure and worker-start unwind
  // all preserve the same grant until cleanup succeeds, without reacquisition.
  for (unsigned failure = 0; failure < 4; ++failure) {
    partialAcquire = failure == 0; invalidApi = failure == 1;
    subscribeOkay = failure != 2; createOkay = failure != 3;
    releaseOkay = false;
    assert(!nativeTouchResume() && !api && lease.grant.slot);
    const auto retained = lease.grant;
    const unsigned beforeRetry = acquisitions;
    nowMs += 2000;
    nativeTouchTick(); assert(!nativeTouchResume());
    assert(lease.grant.generation == retained.generation && acquisitions == beforeRetry);
    partialAcquire = invalidApi = false; subscribeOkay = createOkay = releaseOkay = true;
    assert(nativeTouchResume() && acquisitions == beforeRetry + 1);
    assert(nativeTouchSuspend());
  }

  // An unsuccessful worker start can fail at unsubscribe before release too.
  createOkay = unsubscribeOkay = false;
  const unsigned beforeStartRelease = releases;
  assert(!nativeTouchResume() && api && subscription && lease.grant.slot);
  assert(releases == beforeStartRelease && !workerTask);
  createOkay = unsubscribeOkay = true;
  assert(nativeTouchResume()); assert(nativeTouchSuspend());

#if defined(BOARD_XTEINK_X4_PRO)
  const unsigned beforeBootstrap = acquisitions, releasesBeforeBootstrap = releases;
  revoked = false; sharedGrant = true;
  assert(!nativeTouchAttachBootstrap(&provider));
  sharedGrant = false;
  // A clean failed start leaves attachment retryable, without graph release.
  createOkay = false;
  assert(!nativeTouchAttachBootstrap(&provider));
  assert(!bootstrapAttached && !api && !subscription && !liveSubscription);
  // Bootstrap failure must also retain an unacknowledged subscription.
  createOkay = unsubscribeOkay = false;
  assert(!nativeTouchAttachBootstrap(&provider));
  assert(bootstrapAttached && api == &provider && subscription && !workerTask);
  createOkay = unsubscribeOkay = true;
  assert(nativeTouchResume() && nativeTouchAvailable());
  assert(!nativeTouchAttachBootstrap(&provider));
  assert(nativeTouchSuspend() && !subscription && api == &provider);
  assert(nativeTouchResume() && subscription);
  assert(nativeTouchSuspend());
  assert(acquisitions == beforeBootstrap && releases == releasesBeforeBootstrap);
#endif
  puts("Complete touch consumer acquisition, worker stop, retained cleanup and resume lifetime: PASS");
}
