#include "NativeTouchInput.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <RiscTouchV1.h>

#include "runtime/drivers/InstalledProviderGraph.h"

namespace {
constexpr uint16_t kSwipeThreshold = 25;
constexpr uint8_t kTapDepth = 16;
constexpr uint8_t kSwipeDepth = 8;
constexpr uint8_t kHomeDepth = 16;
constexpr uint32_t kRetryMs = 1000;

struct SwipeEvent {
  NativeTouchPoint start;
  NativeTouchPoint end;
};

RuntimeInstalledProviders::Lease lease;
const risc_touch_api_v1* api = nullptr;
uint64_t subscription = 0;
bool enabled = true;
bool quarantined = false;
uint32_t lastAttemptMs = 0;

bool touchActive = false;
bool touchMoved = false;
bool gestureEligible = false;
uint8_t activeContactId = 0;
uint32_t touchStartMs = 0;
NativeTouchPoint touchStart{};
NativeTouchPoint currentTouch{};

NativeTouchPoint taps[kTapDepth]{};
uint8_t tapHead = 0, tapCount = 0;
SwipeEvent swipes[kSwipeDepth]{};
uint8_t swipeHead = 0, swipeCount = 0;
uint8_t homeCount = 0;

void clearTransient() {
  touchActive = false;
  touchMoved = false;
  gestureEligible = false;
  activeContactId = 0;
  touchStartMs = 0;
  touchStart = {};
  currentTouch = {};
  tapHead = tapCount = 0;
  swipeHead = swipeCount = 0;
  homeCount = 0;
}

void pushTap(const NativeTouchPoint& point) {
  if (tapCount == kTapDepth) {
    tapHead = static_cast<uint8_t>((tapHead + 1u) % kTapDepth);
    --tapCount;
  }
  const uint8_t tail = static_cast<uint8_t>((tapHead + tapCount) % kTapDepth);
  taps[tail] = point;
  ++tapCount;
}

void pushSwipe(const NativeTouchPoint& start, const NativeTouchPoint& end) {
  if (swipeCount == kSwipeDepth) {
    swipeHead = static_cast<uint8_t>((swipeHead + 1u) % kSwipeDepth);
    --swipeCount;
  }
  const uint8_t tail = static_cast<uint8_t>((swipeHead + swipeCount) % kSwipeDepth);
  swipes[tail] = {start, end};
  ++swipeCount;
}

void resync() {
  if (!api) {
    clearTransient();
    return;
  }
  risc_touch_snapshot_v1 snapshot{};
  if (!api->snapshot(api->context, &snapshot) ||
      snapshot.contact_count > RISC_TOUCH_MAX_CONTACTS) {
    clearTransient();
    return;
  }

  // A snapshot after startup/gap is authoritative for held state, but cannot
  // prove how the gesture began. Do not synthesize tap/swipe on its release.
  tapHead = tapCount = 0;
  swipeHead = swipeCount = 0;
  homeCount = 0;
  touchMoved = false;
  gestureEligible = false;
  if (!snapshot.contact_count) {
    touchActive = false;
    activeContactId = 0;
    return;
  }

  touchActive = true;
  activeContactId = snapshot.contacts[0].id;
  touchStart = {snapshot.contacts[0].x, snapshot.contacts[0].y};
  currentTouch = touchStart;
  touchStartMs = static_cast<uint32_t>(snapshot.timestamp_ms);
}

void process(const risc_touch_event_v1& event) {
  if (event.kind == RISC_TOUCH_EVENT_BUTTON_DOWN) {
    if (event.id == 0u && homeCount < kHomeDepth) ++homeCount;
    return;
  }
  if (event.kind == RISC_TOUCH_EVENT_BUTTON_UP) return;

  const NativeTouchPoint point{event.x, event.y};
  if (event.kind == RISC_TOUCH_EVENT_DOWN) {
    if (!touchActive) {
      touchActive = true;
      touchMoved = false;
      gestureEligible = true;
      activeContactId = event.id;
      touchStartMs = static_cast<uint32_t>(event.timestamp_ms);
      touchStart = currentTouch = point;
    }
    return;
  }

  if (!touchActive || event.id != activeContactId) return;

  if (event.kind == RISC_TOUCH_EVENT_MOVE) {
    currentTouch = point;
    const int dx = static_cast<int>(currentTouch.x) - static_cast<int>(touchStart.x);
    const int dy = static_cast<int>(currentTouch.y) - static_cast<int>(touchStart.y);
    if (abs(dx) >= kSwipeThreshold || abs(dy) >= kSwipeThreshold) touchMoved = true;
    return;
  }

  if (event.kind == RISC_TOUCH_EVENT_UP) {
    currentTouch = point;
    if (gestureEligible) {
      const int dx = static_cast<int>(currentTouch.x) - static_cast<int>(touchStart.x);
      const int dy = static_cast<int>(currentTouch.y) - static_cast<int>(touchStart.y);
      if (touchMoved || abs(dx) >= kSwipeThreshold || abs(dy) >= kSwipeThreshold)
        pushSwipe(touchStart, currentTouch);
      else
        pushTap(touchStart);
    }
    touchActive = false;
    touchMoved = false;
    gestureEligible = false;
    activeContactId = 0;
  }
}

bool activate() {
  if (api) return true;
  if (!enabled || quarantined || !Storage.ready()) return false;
  const uint32_t now = millis();
  if (lastAttemptMs && static_cast<uint32_t>(now - lastAttemptMs) < kRetryMs) return false;
  lastAttemptMs = now;

  RuntimeInstalledProviders::Lease candidate{};
  if (!RuntimeInstalledProviders::acquireCapability("input.touch.raw", RISC_TOUCH_API_V1, &candidate)) {
    LOG_DBG("INPUT", "Touch provider unavailable: %s", RuntimeInstalledProviders::lastError());
    return false;
  }

  const auto* candidateApi = static_cast<const risc_touch_api_v1*>(candidate.interface);
  if (!candidateApi || candidateApi->api_version != RISC_TOUCH_API_V1 ||
      candidateApi->struct_size < sizeof(*candidateApi) ||
      !candidateApi->subscribe || !candidateApi->unsubscribe ||
      !candidateApi->poll || !candidateApi->next || !candidateApi->snapshot) {
    (void)RuntimeInstalledProviders::release(&candidate);
    LOG_ERR("INPUT", "Touch provider exposed an invalid API");
    return false;
  }

  const uint64_t candidateSubscription = candidateApi->subscribe(candidateApi->context);
  if (!candidateSubscription) {
    (void)RuntimeInstalledProviders::release(&candidate);
    LOG_ERR("INPUT", "Touch provider subscription failed");
    return false;
  }

  lease = candidate;
  api = candidateApi;
  subscription = candidateSubscription;
  clearTransient();
  resync();
  LOG_INF("INPUT", "Touch provider ready");
  return true;
}
}  // namespace

void nativeTouchTick() {
  if (!activate()) return;

  const bool pollOk = api->poll(api->context, 16u);
  bool needResync = !pollOk;
  for (unsigned i = 0; i < RISC_TOUCH_QUEUE_LENGTH; ++i) {
    risc_touch_event_v1 event{};
    const int32_t result = api->next(api->context, subscription, &event);
    if (result == 0) break;
    if (result < 0) {
      needResync = true;
      break;
    }
    process(event);
  }
  if (needResync) resync();
}

bool nativeTouchSuspend() {
  enabled = false;
  clearTransient();
  if (!api) {
    subscription = 0;
    lease = {};
    return true;
  }

  bool ok = true;
  if (subscription && !api->unsubscribe(api->context, subscription)) ok = false;
  subscription = 0;
  RuntimeInstalledProviders::Lease grant = lease;
  lease = {};
  api = nullptr;
  if (!RuntimeInstalledProviders::release(&grant)) ok = false;
  if (!ok) {
    quarantined = true;
    LOG_ERR("INPUT", "Touch provider failed to quiesce");
  }
  return ok;
}

bool nativeTouchResume() {
  enabled = true;
  if (!api) lastAttemptMs = 0;
  return activate();
}

bool nativeTouchAvailable() { return api != nullptr && subscription != 0; }

bool nativeTouchHadActivity() {
  return touchActive || tapCount || swipeCount || homeCount;
}

bool nativeTouchGetTap(NativeTouchPoint& point) {
  if (!tapCount) return false;
  point = taps[tapHead];
  tapHead = static_cast<uint8_t>((tapHead + 1u) % kTapDepth);
  --tapCount;
  return true;
}

bool nativeTouchGetHold(NativeTouchPoint& point, unsigned long& heldMs) {
  if (!touchActive || touchMoved || !gestureEligible) return false;
  point = currentTouch;
  heldMs = static_cast<uint32_t>(millis() - touchStartMs);
  return true;
}

bool nativeTouchGetSwipe(NativeTouchPoint& start, NativeTouchPoint& end) {
  if (!swipeCount) return false;
  const SwipeEvent event = swipes[swipeHead];
  swipeHead = static_cast<uint8_t>((swipeHead + 1u) % kSwipeDepth);
  --swipeCount;
  start = event.start;
  end = event.end;
  return true;
}

bool nativeTouchTakeHomePress() {
  if (!homeCount) return false;
  --homeCount;
  return true;
}
