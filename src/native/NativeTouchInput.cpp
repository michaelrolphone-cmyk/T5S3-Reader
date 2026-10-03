#include "NativeTouchInput.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <RiscTouchV1.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "runtime/drivers/InstalledProviderGraph.h"

namespace {
constexpr uint16_t kSwipeThreshold = 25;
constexpr uint8_t kTapDepth = 16;
constexpr uint8_t kSwipeDepth = 8;
constexpr uint8_t kHomeDepth = 16;
constexpr uint32_t kRetryMs = 1000;
constexpr uint32_t kCaptureIntervalMs = 5;
constexpr uint32_t kPollFailureTimeoutMs = 1000;
constexpr uint32_t kWorkerStopTimeoutMs = 100;

struct SwipeEvent {
  NativeTouchPoint start;
  NativeTouchPoint end;
};

RuntimeInstalledProviders::Lease lease;
const risc_touch_api_v1* api = nullptr;
uint64_t subscription = 0;
bool enabled = true;
bool quarantined = false;
bool bootstrapAttached = false;
uint32_t lastAttemptMs = 0;

TaskHandle_t workerTask = nullptr;
bool workerRequested = false;
portMUX_TYPE touchStateMux = portMUX_INITIALIZER_UNLOCKED;

bool touchActive = false;
bool activityThisTick = false;
bool touchMoved = false;
bool gestureEligible = false;
uint8_t activeContactId = 0;
uint32_t touchStartMs = 0;
NativeTouchPoint touchStart{};
NativeTouchPoint currentTouch{};
uint32_t activitySerial = 0;
uint32_t observedActivitySerial = 0;
uint64_t consumedSequence = 0;
uint32_t focusRequested = 0, focusApplied = 0;
bool pollFailureActive = false;
bool pollFailureExpired = false;
uint32_t pollFailureStartMs = 0;
NativeTouchDiagnostics diagnostics{};
uint32_t lastServiceStartMs = 0;
bool serviceStarted = false;

NativeTouchPoint taps[kTapDepth]{};
uint8_t tapHead = 0, tapCount = 0;
SwipeEvent swipes[kSwipeDepth]{};
uint8_t swipeHead = 0, swipeCount = 0;
uint8_t homeCount = 0;

void clearTransientLocked(bool clearQueues) {
  touchActive = false;
  activityThisTick = false;
  touchMoved = false;
  gestureEligible = false;
  activeContactId = 0;
  touchStartMs = 0;
  touchStart = {};
  currentTouch = {};
  if (clearQueues) {
    tapHead = tapCount = 0;
    swipeHead = swipeCount = 0;
    homeCount = 0;
  }
}

void clearTransient(bool clearQueues = true) {
  portENTER_CRITICAL(&touchStateMux);
  clearTransientLocked(clearQueues);
  if (clearQueues) {
    // Full reset starts a new subscription lifetime. Its provider may have
    // restarted sequence numbering, and the initial snapshot can fail busy.
    consumedSequence = 0;
    observedActivitySerial = activitySerial;
  }
  portEXIT_CRITICAL(&touchStateMux);
}

void pushTapLocked(const NativeTouchPoint& point) {
  ++diagnostics.taps;
  if (tapCount == kTapDepth) {
    ++diagnostics.tapOverflows;
    tapHead = static_cast<uint8_t>((tapHead + 1u) % kTapDepth);
    --tapCount;
  }
  const uint8_t tail = static_cast<uint8_t>((tapHead + tapCount) % kTapDepth);
  taps[tail] = point;
  ++tapCount;
}

void pushSwipeLocked(const NativeTouchPoint& start, const NativeTouchPoint& end) {
  if (swipeCount == kSwipeDepth) {
    swipeHead = static_cast<uint8_t>((swipeHead + 1u) % kSwipeDepth);
    --swipeCount;
  }
  const uint8_t tail = static_cast<uint8_t>((swipeHead + swipeCount) % kSwipeDepth);
  swipes[tail] = {start, end};
  ++swipeCount;
}

void applySnapshotLocked(const risc_touch_snapshot_v1& snapshot, bool clearQueues) {
  consumedSequence = snapshot.sequence;
  if (clearQueues) {
    tapHead = tapCount = 0;
    swipeHead = swipeCount = 0;
    homeCount = 0;
  }

  // A snapshot after startup/gap is authoritative for held state, but cannot
  // prove how the gesture began. Never synthesize a tap/swipe from it.
  touchMoved = false;
  gestureEligible = false;
  if (!snapshot.contact_count) {
    touchActive = false;
    activeContactId = 0;
    touchStartMs = 0;
    touchStart = {};
    currentTouch = {};
  } else {
    touchActive = true;
    activeContactId = snapshot.contacts[0].id;
    touchStart = {snapshot.contacts[0].x, snapshot.contacts[0].y};
    currentTouch = touchStart;
    touchStartMs = static_cast<uint32_t>(snapshot.timestamp_ms);
  }
}

void applySnapshot(const risc_touch_snapshot_v1& snapshot, bool clearQueues) {
  portENTER_CRITICAL(&touchStateMux);
  applySnapshotLocked(snapshot, clearQueues);
  portEXIT_CRITICAL(&touchStateMux);
}

bool resync(bool clearQueues) {
  if (!api) {
    clearTransient(clearQueues);
    return false;
  }

  risc_touch_snapshot_v1 snapshot{};
  if (!api->snapshot(api->context, &snapshot) ||
      snapshot.contact_count > RISC_TOUCH_MAX_CONTACTS) {
    // A transient provider/I2C fault invalidates only the in-flight gesture.
    // Completed taps/swipes already captured for the UI must not be discarded.
    clearTransient(clearQueues);
    return false;
  }

  applySnapshot(snapshot, clearQueues);
  return true;
}

void process(const risc_touch_event_v1& event) {
  portENTER_CRITICAL(&touchStateMux);
  // Another subscriber can poll between our GAP and snapshot. Events already
  // represented by that snapshot must never replay as fresh user gestures.
  if (event.sequence <= consumedSequence) {
    portEXIT_CRITICAL(&touchStateMux);
    return;
  }
  consumedSequence = event.sequence;
  // A focus transition invalidates both delivered gestures and raw events
  // still waiting in the provider's subscriber queue. Only the capture task
  // can establish the post-poll snapshot fence; no UI task touches its lease.
  if (focusRequested != focusApplied) {
    portEXIT_CRITICAL(&touchStateMux);
    return;
  }
  ++activitySerial;
  ++diagnostics.events;

  if (event.kind == RISC_TOUCH_EVENT_BUTTON_DOWN) {
    if (event.id == 0u && homeCount < kHomeDepth) ++homeCount;
    portEXIT_CRITICAL(&touchStateMux);
    return;
  }
  if (event.kind == RISC_TOUCH_EVENT_BUTTON_UP) {
    portEXIT_CRITICAL(&touchStateMux);
    return;
  }

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
    portEXIT_CRITICAL(&touchStateMux);
    return;
  }

  if (!touchActive || event.id != activeContactId) {
    portEXIT_CRITICAL(&touchStateMux);
    return;
  }

  if (event.kind == RISC_TOUCH_EVENT_MOVE) {
    currentTouch = point;
    const int dx = static_cast<int>(currentTouch.x) - static_cast<int>(touchStart.x);
    const int dy = static_cast<int>(currentTouch.y) - static_cast<int>(touchStart.y);
    if (abs(dx) >= kSwipeThreshold || abs(dy) >= kSwipeThreshold) touchMoved = true;
    portEXIT_CRITICAL(&touchStateMux);
    return;
  }

  if (event.kind == RISC_TOUCH_EVENT_UP) {
    currentTouch = point;
    if (gestureEligible) {
      const int dx = static_cast<int>(currentTouch.x) - static_cast<int>(touchStart.x);
      const int dy = static_cast<int>(currentTouch.y) - static_cast<int>(touchStart.y);
      if (touchMoved || abs(dx) >= kSwipeThreshold || abs(dy) >= kSwipeThreshold)
        pushSwipeLocked(touchStart, currentTouch);
      else
        pushTapLocked(touchStart);
    }
    touchActive = false;
    touchMoved = false;
    gestureEligible = false;
    activeContactId = 0;
  }
  portEXIT_CRITICAL(&touchStateMux);
}

void serviceProvider() {
  if (!api || !subscription) return;
  const uint32_t serviceStart = millis();
  portENTER_CRITICAL(&touchStateMux);
  ++diagnostics.polls;
  if (serviceStarted) {
    const uint32_t gap = serviceStart - lastServiceStartMs;
    if (gap > diagnostics.maxCaptureGapMs) diagnostics.maxCaptureGapMs = gap;
  }
  lastServiceStartMs = serviceStart;
  serviceStarted = true;
  portEXIT_CRITICAL(&touchStateMux);

  // One report per turn bounds bus work and avoids a second speculative I/O
  // failure delaying a report already published by this poll. A failed poll
  // may still have queued valid events: always drain them before recovery.
  bool pollOk = api->poll(api->context, 1u);
  portENTER_CRITICAL(&touchStateMux);
  const uint32_t requestedFocus = focusRequested;
  const bool fenceNeeded = requestedFocus != focusApplied;
  portEXIT_CRITICAL(&touchStateMux);
  if (fenceNeeded) {
    risc_touch_snapshot_v1 snapshot{};
    // Failed physical poll cannot prove that an old held contact was sampled.
    // Keep input fenced, retrying on later capture turns, without unloading.
    if (pollOk && api->snapshot(api->context, &snapshot) &&
        snapshot.contact_count <= RISC_TOUCH_MAX_CONTACTS) {
      portENTER_CRITICAL(&touchStateMux);
      if (focusRequested == requestedFocus) {
        applySnapshotLocked(snapshot, true);
        focusApplied = requestedFocus;
      }
      portEXIT_CRITICAL(&touchStateMux);
    }
  }
  for (unsigned i = 0; i < RISC_TOUCH_QUEUE_LENGTH; ++i) {
    risc_touch_event_v1 event{};
    const int32_t result = api->next(api->context, subscription, &event);
    if (result == 0) break;
    if (result == -2) {
      pollOk = false;  // Temporary access/provider fault, not proven data loss.
      break;
    }
    if (result < 0) {
      portENTER_CRITICAL(&touchStateMux);
      ++diagnostics.gaps;
      portEXIT_CRITICAL(&touchStateMux);
      // Queue GAP/stale subscription is explicit stream invalidation and
      // requires an authoritative snapshot immediately.
      (void)resync(false);
      break;
    }
    process(event);
  }

  if (pollOk) {
    pollFailureActive = pollFailureExpired = false;
  } else {
    const uint32_t now = millis();
    if (!pollFailureActive) {
      pollFailureActive = true;
      pollFailureStartMs = now;
    }
    // A transport miss is not a stream GAP. The provider snapshot is cached
    // state, so resnapshotting after three misses merely loses a known DOWN.
    // Permit short retries, but cancel held/unfinished gestures after a real
    // outage. Completed gestures remain queued and no release is synthesized.
    if (!pollFailureExpired &&
        static_cast<uint32_t>(now - pollFailureStartMs) >= kPollFailureTimeoutMs) {
      clearTransient(false);
      pollFailureExpired = true;
      portENTER_CRITICAL(&touchStateMux);
      ++diagnostics.outages;
      portEXIT_CRITICAL(&touchStateMux);
    }
  }
  const uint32_t duration = millis() - serviceStart;
  portENTER_CRITICAL(&touchStateMux);
  if (!pollOk) ++diagnostics.pollFailures;
  if (duration > diagnostics.maxServiceMs) diagnostics.maxServiceMs = duration;
  portEXIT_CRITICAL(&touchStateMux);
}

bool workerShouldRun() {
  portENTER_CRITICAL(&touchStateMux);
  const bool run = workerRequested;
  portEXIT_CRITICAL(&touchStateMux);
  return run;
}

void touchWorker(void*) {
  while (workerShouldRun()) {
    serviceProvider();
    (void)ulTaskNotifyTake(pdTRUE, (pdMS_TO_TICKS(kCaptureIntervalMs) ? pdMS_TO_TICKS(kCaptureIntervalMs) : 1));
  }

  portENTER_CRITICAL(&touchStateMux);
  workerTask = nullptr;
  portEXIT_CRITICAL(&touchStateMux);
  vTaskDelete(nullptr);
}

bool startWorker() {
  portENTER_CRITICAL(&touchStateMux);
  if (workerTask) {
    workerRequested = true;
    portEXIT_CRITICAL(&touchStateMux);
    return true;
  }
  workerRequested = true;
  portEXIT_CRITICAL(&touchStateMux);

  TaskHandle_t task = nullptr;
  if (xTaskCreate(touchWorker, "touch-provider", 4096, nullptr, 4, &task) != pdPASS) {
    portENTER_CRITICAL(&touchStateMux);
    workerRequested = false;
    portEXIT_CRITICAL(&touchStateMux);
    LOG_ERR("INPUT", "Touch provider capture task failed to start");
    return false;
  }

  portENTER_CRITICAL(&touchStateMux);
  workerTask = task;
  portEXIT_CRITICAL(&touchStateMux);
  return true;
}

bool stopWorker() {
  TaskHandle_t task = nullptr;
  portENTER_CRITICAL(&touchStateMux);
  workerRequested = false;
  task = workerTask;
  portEXIT_CRITICAL(&touchStateMux);
  if (!task) return true;

  xTaskNotifyGive(task);
  const uint32_t started = millis();
  while (static_cast<uint32_t>(millis() - started) < kWorkerStopTimeoutMs) {
    portENTER_CRITICAL(&touchStateMux);
    const bool stopped = workerTask == nullptr;
    portEXIT_CRITICAL(&touchStateMux);
    if (stopped) return true;
    delay(1);
  }
  return false;
}

bool activate() {
  if (api) return startWorker();
  if (!enabled || quarantined) return false; // Graph may use the read-only boot store.
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
  pollFailureActive = pollFailureExpired = false;
  clearTransient();
  serviceStarted = false;
  (void)resync(true);
  if (!startWorker()) {
    const bool unsubscribed = api->unsubscribe(api->context, subscription);
    subscription = 0;
    RuntimeInstalledProviders::Lease grant = lease;
    lease = {};
    api = nullptr;
    const bool released = RuntimeInstalledProviders::release(&grant);
    if (!unsubscribed || !released) quarantined = true;
    return false;
  }

  LOG_INF("INPUT", "Touch provider ready with independent capture task");
  return true;
}
}  // namespace

void nativeTouchTick() {
  // Provider sampling happens independently at 5 ms cadence. The owner/UI loop
  // only activates the provider and snapshots whether physical activity arrived
  // since its previous tick, so display latency cannot cause missed short taps.
  if (!activate()) {
    portENTER_CRITICAL(&touchStateMux);
    activityThisTick = false;
    portEXIT_CRITICAL(&touchStateMux);
    return;
  }

  portENTER_CRITICAL(&touchStateMux);
  activityThisTick = activitySerial != observedActivitySerial;
  observedActivitySerial = activitySerial;
  const NativeTouchDiagnostics stats = diagnostics;
  portEXIT_CRITICAL(&touchStateMux);

  // Report from the UI owner, never perform log/SD work in the capture task.
  // Existing system logs can distinguish bus failures, stream loss and a
  // stalled consumer without requiring a serial cable while USB is in use.
  static uint32_t lastLogMs = 0, reportedFailures = 0, reportedGaps = 0;
  static uint32_t reportedOverflows = 0, reportedOutages = 0;
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastLogMs) >= 5000u &&
      (stats.pollFailures != reportedFailures || stats.gaps != reportedGaps ||
       stats.tapOverflows != reportedOverflows || stats.outages != reportedOutages)) {
    lastLogMs = now;
    reportedFailures = stats.pollFailures;
    reportedGaps = stats.gaps;
    reportedOverflows = stats.tapOverflows;
    reportedOutages = stats.outages;
    LOG_INF("TOUCH", "polls=%lu fail=%lu gaps=%lu events=%lu taps=%lu overflow=%lu outage=%lu maxService=%lu maxGap=%lu",
            static_cast<unsigned long>(stats.polls), static_cast<unsigned long>(stats.pollFailures),
            static_cast<unsigned long>(stats.gaps), static_cast<unsigned long>(stats.events),
            static_cast<unsigned long>(stats.taps), static_cast<unsigned long>(stats.tapOverflows),
            static_cast<unsigned long>(stats.outages), static_cast<unsigned long>(stats.maxServiceMs),
            static_cast<unsigned long>(stats.maxCaptureGapMs));
  }
}

bool nativeTouchSuspend() {
  enabled = false;
  if (!stopWorker()) {
    quarantined = true;
    LOG_ERR("INPUT", "Touch provider capture task failed to stop");
    return false;
  }

  pollFailureActive = pollFailureExpired = false;
  clearTransient();
  if (!api) {
    subscription = 0;
    lease = {};
    return true;
  }

  bool ok = true;
  if (subscription && !api->unsubscribe(api->context, subscription)) ok = false;
  subscription = 0;
#if defined(BOARD_XTEINK_X4_PRO)
  if (bootstrapAttached) {
    // The X4 boot controller owns the provider module and its bus/rail. Only
    // this consumer's subscription is released here.
    if (!ok) quarantined = true;
    return ok;
  }
#endif
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
#if defined(BOARD_XTEINK_X4_PRO)
  if (bootstrapAttached && api && !subscription && !quarantined) {
    subscription = api->subscribe(api->context);
    if (!subscription) return false;
    clearTransient();
    (void)resync(true);
  }
#endif
  if (!api) lastAttemptMs = 0;
  return activate();
}

#if defined(BOARD_XTEINK_X4_PRO)
bool nativeTouchAttachBootstrap(const risc_touch_api_v1* candidate) {
  if (api || bootstrapAttached || quarantined || lease.grant.slot ||
      RuntimeInstalledProviders::hasLiveGrants() || !candidate ||
      candidate->api_version != RISC_TOUCH_API_V1 ||
      candidate->struct_size < sizeof(*candidate) || !candidate->subscribe ||
      !candidate->unsubscribe || !candidate->poll || !candidate->next ||
      !candidate->snapshot) return false;
  const uint64_t token = candidate->subscribe(candidate->context);
  if (!token) return false;
  api = candidate;
  subscription = token;
  clearTransient();
  serviceStarted = false;
  (void)resync(true);
  if (!startWorker()) {
    (void)candidate->unsubscribe(candidate->context, token);
    api = nullptr;
    subscription = 0;
    return false;
  }
  bootstrapAttached = true;
  enabled = true;
  LOG_INF("INPUT", "X4 touch bootstrap attached");
  return true;
}
#endif

bool nativeTouchAvailable() {
  portENTER_CRITICAL(&touchStateMux);
  const bool worker = workerTask != nullptr;
  portEXIT_CRITICAL(&touchStateMux);
  return api != nullptr && subscription != 0 && worker;
}

bool nativeTouchHadActivity() {
  portENTER_CRITICAL(&touchStateMux);
  // A physically held contact keeps the device awake. Completed gestures do
  // not remain sticky, but activity that arrived after the latest owner tick
  // still counts until the next tick observes its serial.
  const bool active = touchActive || activityThisTick ||
                      activitySerial != observedActivitySerial;
  portEXIT_CRITICAL(&touchStateMux);
  return active;
}

void nativeTouchDiscardGestures() {
  portENTER_CRITICAL(&touchStateMux);
  ++focusRequested;
  tapHead = tapCount = 0;
  swipeHead = swipeCount = 0;
  homeCount = 0;
  gestureEligible = false;  // A held contact must lift before it can become a tap.
  portEXIT_CRITICAL(&touchStateMux);
}

bool nativeTouchGetTap(NativeTouchPoint& point) {
  portENTER_CRITICAL(&touchStateMux);
  if (!tapCount) {
    portEXIT_CRITICAL(&touchStateMux);
    return false;
  }
  point = taps[tapHead];
  tapHead = static_cast<uint8_t>((tapHead + 1u) % kTapDepth);
  --tapCount;
  portEXIT_CRITICAL(&touchStateMux);
  return true;
}

bool nativeTouchGetContact(NativeTouchPoint& point) {
  portENTER_CRITICAL(&touchStateMux);
  const bool active = touchActive && gestureEligible;
  if (active) point = currentTouch;
  portEXIT_CRITICAL(&touchStateMux);
  return active;
}

bool nativeTouchGetHold(NativeTouchPoint& point, unsigned long& heldMs) {
  uint32_t started = 0;
  portENTER_CRITICAL(&touchStateMux);
  if (!touchActive || touchMoved || !gestureEligible) {
    portEXIT_CRITICAL(&touchStateMux);
    return false;
  }
  point = currentTouch;
  started = touchStartMs;
  portEXIT_CRITICAL(&touchStateMux);
  heldMs = static_cast<uint32_t>(millis() - started);
  return true;
}

bool nativeTouchGetSwipe(NativeTouchPoint& start, NativeTouchPoint& end) {
  portENTER_CRITICAL(&touchStateMux);
  if (!swipeCount) {
    portEXIT_CRITICAL(&touchStateMux);
    return false;
  }
  const SwipeEvent event = swipes[swipeHead];
  swipeHead = static_cast<uint8_t>((swipeHead + 1u) % kSwipeDepth);
  --swipeCount;
  portEXIT_CRITICAL(&touchStateMux);
  start = event.start;
  end = event.end;
  return true;
}

bool nativeTouchTakeHomePress() {
  portENTER_CRITICAL(&touchStateMux);
  if (!homeCount) {
    portEXIT_CRITICAL(&touchStateMux);
    return false;
  }
  --homeCount;
  portEXIT_CRITICAL(&touchStateMux);
  return true;
}

NativeTouchDiagnostics nativeTouchDiagnostics() {
  portENTER_CRITICAL(&touchStateMux);
  const NativeTouchDiagnostics result = diagnostics;
  portEXIT_CRITICAL(&touchStateMux);
  return result;
}
