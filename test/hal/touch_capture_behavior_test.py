#!/usr/bin/env python3
"""Execute production gesture/service code with a scripted raw-touch provider."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/native/NativeTouchInput.cpp').read_text()
core = source[source.index('namespace {'):source.index('bool workerShouldRun()')]
getters = source[source.index('void nativeTouchDiscardGestures('):]
prefix = r'''
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <cstdio>
#include "NativeTouchInput.h"
#include "RiscTouchV1.h"
namespace RuntimeInstalledProviders { struct Lease {}; }
using TaskHandle_t = void*;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
static uint32_t nowMs;
static uint32_t millis() { return nowMs; }
'''
fixture = r'''
static std::deque<risc_touch_event_v1> events;
static bool pollOk = true, gap = false, busyNext = false;
static uint64_t serial;
static bool snapshotRace, snapshotFails, homeSnapshotRace;
static risc_touch_snapshot_v1 state{};
static bool mockPoll(void*, size_t) { return pollOk; }
static int32_t mockNext(void*, uint64_t, risc_touch_event_v1* out) {
  if (busyNext) return -2;
  if (gap) { gap = false; events.clear(); return -1; }
  if (events.empty()) return 0;
  *out = events.front(); events.pop_front(); return 1;
}
static bool mockSnapshot(void*, risc_touch_snapshot_v1* out) {
  if (snapshotFails) return false;
  if (homeSnapshotRace) {
    homeSnapshotRace=false;
    risc_touch_event_v1 e{};e.sequence=++serial;e.timestamp_ms=nowMs;
    e.kind=RISC_TOUCH_EVENT_BUTTON_DOWN;e.id=0;events.push_back(e);
    state.sequence=serial;
  }
  if (snapshotRace) {
    snapshotRace=false;
    risc_touch_event_v1 e{};
    e.sequence=++serial; e.kind=RISC_TOUCH_EVENT_DOWN; e.id=1; e.x=100; e.y=200;
    events.push_back(e);
    e.sequence=++serial; e.kind=RISC_TOUCH_EVENT_UP; events.push_back(e);
    state.sequence=serial; state.contact_count=0;
  }
  *out = state; return true;
}
static const risc_touch_api_v1 provider = {
  1, sizeof(provider), nullptr, nullptr, nullptr, mockPoll, mockNext, mockSnapshot
};
static void queue(uint8_t kind, uint16_t x=100, uint16_t y=200) {
  risc_touch_event_v1 event{};
  event.sequence = ++serial; event.timestamp_ms = nowMs;
  event.kind = kind; event.id = 1; event.x = x; event.y = y;
  events.push_back(event);
  state.sequence = serial; state.timestamp_ms = nowMs;
  state.contact_count = kind == RISC_TOUCH_EVENT_UP ? 0 : 1;
  state.contacts[0] = {1, 0, x, y};
}
int main(int argc, char**) {
  api = &provider; subscription = 1; nowMs = 100;
  NativeTouchPoint p{}, end{};
  // Home presses retain provider capture times through delayed UI delivery.
  risc_touch_event_v1 home{};
  home.kind = RISC_TOUCH_EVENT_BUTTON_DOWN; home.id = 0;
  for (unsigned i=0; i<20; ++i) {
    home.sequence = ++serial; home.timestamp_ms = 100 + i * 200;
    process(home);
  }
  unsigned long captured = 0;
  nowMs = 10000;
  for (unsigned i=0; i<16; ++i) {
    assert(nativeTouchTakeHomePress(captured) && captured == 100 + i * 200);
  }
  assert(!nativeTouchTakeHomePress(captured));
  // Ring wrap and focus reset must not replay old Home events.
  home.sequence=++serial; home.timestamp_ms=12345; process(home);
  assert(nativeTouchTakeHomePress(captured) && captured==12345);
  home.sequence=++serial; process(home);
  nativeTouchDiscardGestures(); assert(!nativeTouchTakeHomePress(captured));
  serviceProvider(); nowMs=100;
  if (argc > 1) {
    // A batch can publish events and then fail. Deliver the completed tap now.
    queue(RISC_TOUCH_EVENT_DOWN); queue(RISC_TOUCH_EVENT_UP);
    pollOk = false; serviceProvider();
    assert(nativeTouchGetTap(p));
    puts("partial failed poll still delivers captured tap PASS"); return 0;
  }
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider();
  for (int i=0; i<6; ++i) { nowMs += 20; pollOk=false; serviceProvider(); }
  pollOk=true; queue(RISC_TOUCH_EVENT_UP); serviceProvider();
  assert(nativeTouchGetTap(p) && p.x==100 && p.y==200);
  assert(!nativeTouchGetTap(p));
  // An unresolved display transform blocks coordinate delivery, not capture/Home.
  nativeTouchSuppressCoordinates(true);serviceProvider();
  const auto suppressFocus=focusRequested;
  nativeTouchSuppressCoordinates(true);assert(focusRequested==suppressFocus);
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();
  assert(!nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();
  unsigned long suppressedHeld=0;
  assert(!nativeTouchGetContact(p)&&!nativeTouchGetHold(p,suppressedHeld));
  queue(RISC_TOUCH_EVENT_MOVE,180,200);queue(RISC_TOUCH_EVENT_UP,180,200);serviceProvider();
  assert(!nativeTouchGetSwipe(p,end));
  home.sequence=++serial;home.timestamp_ms=22345;process(home);
  assert(nativeTouchTakeHomePress(captured)&&captured==22345);
  // Unblock is atomic with discarding the captured queue and fencing raw backlog.
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);
  nativeTouchSuppressCoordinates(false);serviceProvider();
  assert(!nativeTouchGetTap(p)&&!nativeTouchGetSwipe(p,end));
  queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();
  nativeTouchSuppressCoordinates(true);serviceProvider();
  nativeTouchSuppressCoordinates(false);serviceProvider();
  assert(!nativeTouchGetContact(p)&&!nativeTouchGetHold(p,suppressedHeld));
  queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(!nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();
  const auto releasedFocus=focusRequested;
  nativeTouchSuppressCoordinates(false);assert(focusRequested==releasedFocus);
  assert(nativeTouchGetTap(p));
  // Boot handoff drops completed/held loading-screen gestures, then accepts new taps.
  queue(RISC_TOUCH_EVENT_DOWN); queue(RISC_TOUCH_EVENT_UP); serviceProvider();
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider();
  nativeTouchDiscardGestures();serviceProvider();assert(!nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(!nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(nativeTouchGetTap(p));
  // An old tap still in the RAW provider queue must not become a new app tap.
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);
  nativeTouchDiscardGestures();serviceProvider();assert(!nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(nativeTouchGetTap(p));
  // Snapshot failure keeps focus closed; retry establishes a fence without replay.
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);
  nativeTouchDiscardGestures();snapshotFails=true;serviceProvider();assert(!nativeTouchGetTap(p));
  snapshotFails=false;serviceProvider();
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(nativeTouchGetTap(p));
  // Provider lock contention on next() must not resnapshot a valid DOWN.
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider();
  busyNext=true; serviceProvider(); nowMs+=10; serviceProvider(); busyNext=false;
  queue(RISC_TOUCH_EVENT_UP); serviceProvider(); assert(nativeTouchGetTap(p));
  // Long outage must not leave a held contact or produce a delayed phantom tap.
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider();
  pollOk=false; serviceProvider(); nowMs+=1100; serviceProvider();
  unsigned long held=0; assert(!nativeTouchGetHold(p, held));
  pollOk=true; queue(RISC_TOUCH_EVENT_UP); serviceProvider();
  assert(!nativeTouchGetTap(p));
  // Explicit GAP remains an invalidation; normal input recovers afterwards.
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider(); gap=true; serviceProvider();
  queue(RISC_TOUCH_EVENT_UP); serviceProvider(); assert(!nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider();
  queue(RISC_TOUCH_EVENT_UP); serviceProvider(); assert(nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider();
  queue(RISC_TOUCH_EVENT_MOVE, 180, 200); serviceProvider();
  NativeTouchPoint dragPoint{};
  assert(nativeTouchGetContact(dragPoint) && dragPoint.x==180);
  unsigned long dragHeld=0;
  assert(!nativeTouchGetHold(dragPoint,dragHeld));
  queue(RISC_TOUCH_EVENT_UP, 180, 200); serviceProvider();
  assert(nativeTouchGetSwipe(p,end) && p.x==100 && end.x==180);
  assert(!nativeTouchGetTap(p));
  // A concurrent poll after GAP but before snapshot cannot replay old taps.
  gap=true; snapshotRace=true; serviceProvider(); serviceProvider();
  assert(!nativeTouchGetTap(p));
  // Completed tap survives an unrelated later outage.
  queue(RISC_TOUCH_EVENT_DOWN); queue(RISC_TOUCH_EVENT_UP); serviceProvider();
  pollOk=false; serviceProvider(); nowMs+=1100; serviceProvider();
  assert(nativeTouchGetTap(p));
  // Unsigned elapsed-time check also works across millis rollover.
  pollOk=true; serviceProvider(); coordinateConsumerStarted=false; nowMs=UINT32_MAX-50;
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider();
  pollOk=false; serviceProvider(); nowMs=60; serviceProvider();
  pollOk=true; queue(RISC_TOUCH_EVENT_UP); serviceProvider();
  assert(nativeTouchGetTap(p));
  // Activation after a provider restart must discard the old sequence floor,
  // even if contention makes the initial snapshot unavailable.
  assert(consumedSequence > 2);
  clearTransient(); serial=0; state={}; snapshotFails=true;
  assert(!resync(true)); snapshotFails=false;
  queue(RISC_TOUCH_EVENT_DOWN); serviceProvider();
  queue(RISC_TOUCH_EVENT_UP); serviceProvider();
  assert(nativeTouchGetTap(p) && !nativeTouchGetTap(p));

  // The sampler stays live through a 25-second blocked UI; its completed
  // coordinate queue is bounded but no longer replays as an input log.
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_MOVE,180);queue(RISC_TOUCH_EVENT_UP,180);serviceProvider();
  nowMs+=25000;coordinateConsumerStarted=false; // Isolate per-event expiry from consumer-stall policy.
  assert(!nativeTouchGetTap(p)&&!nativeTouchGetSwipe(p,end));
  assert(nativeTouchDiagnostics().expiredGestures==2);
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();
  nowMs+=1500;assert(nativeTouchGetTap(p)); // ordinary short refresh latency
  // A known physical long hold is not a stale queued gesture.
  queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();
  unsigned long longHeld=0;
  for(unsigned i=0;i<30;++i){nowMs+=1000;assert(nativeTouchGetHold(p,longHeld));}
  assert(longHeld==30000);
  queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(nativeTouchGetTap(p));
  // But a raw backlog DOWN must not become a new contact after 20 seconds.
  queue(RISC_TOUCH_EVENT_DOWN);nowMs+=20000;serviceProvider();
  assert(!nativeTouchGetContact(p));
  queue(RISC_TOUCH_EVENT_MOVE,180);queue(RISC_TOUCH_EVENT_UP,180);serviceProvider();
  assert(!nativeTouchGetTap(p)&&!nativeTouchGetSwipe(p,end));
  // Completion timestamps remain valid across millis rollover.
  coordinateConsumerStarted=false;nowMs=UINT32_MAX-100;queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();
  nowMs=100;assert(nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();
  nowMs+=25000;assert(!nativeTouchGetTap(p));
  // Context change stays closed through loading, intermediate popup and a
  // failed destination presentation; retry opens only after render returns.
  nativeTouchBeginSurfaceTransition(true);serviceProvider();
  const auto firstEpoch=nativeTouchPresentationEpoch();
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();
  nativeTouchSurfacePresented(firstEpoch,true);
  assert(!nativeTouchGetTap(p));
  nativeTouchSurfacePresented(firstEpoch,false);
  nativeTouchCompleteSurfaceTransition(firstEpoch);
  assert(surfaceTransitionPending);
  nativeTouchSurfacePresented(firstEpoch,true);
  queue(RISC_TOUCH_EVENT_DOWN);serviceProvider(); // finger held through final frame
  nativeTouchCompleteSurfaceTransition(firstEpoch);serviceProvider();
  assert(!surfaceTransitionPending&&!nativeTouchGetContact(p));
  queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(!nativeTouchGetTap(p));
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(nativeTouchGetTap(p));
  // An outgoing render cannot release a newer context, nor release the
  // independent flip/orientation suppression owned by the display.
  nativeTouchBeginSurfaceTransition();const auto outgoing=nativeTouchPresentationEpoch();
  nativeTouchBeginSurfaceTransition();const auto incoming=nativeTouchPresentationEpoch();
  nativeTouchSurfacePresented(outgoing,true);assert(surfaceTransitionPending);
  nativeTouchSuppressCoordinates(true);nativeTouchSurfacePresented(incoming,true);serviceProvider();
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(!nativeTouchGetTap(p));
  nativeTouchSuppressCoordinates(false);serviceProvider();
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(nativeTouchGetTap(p));
  // Snapshot retries keep coordinates fenced without swallowing Home. Include
  // a second subscriber publishing Home between next(empty) and snapshot.
  nativeTouchBeginSurfaceTransition();homeSnapshotRace=true;serviceProvider();serviceProvider();
  assert(nativeTouchTakeHomePress(captured)&&captured==nowMs);
  assert(!nativeTouchTakeHomePress(captured));
  snapshotFails=true;nativeTouchDiscardGestures();
  home.sequence=++serial;home.timestamp_ms=nowMs;process(home);
  serviceProvider();assert(nativeTouchTakeHomePress(captured));
  snapshotFails=false;serviceProvider();
  nativeTouchSurfacePresented(nativeTouchPresentationEpoch(),true);serviceProvider();
  const auto stats = nativeTouchDiagnostics();
  assert(stats.pollFailures >= 6 && stats.gaps == 2 && stats.outages == 2);
  assert(stats.taps >= 4 && stats.events > stats.taps);
  puts("touch capture transient/outage/GAP/recovery/swipe/rollover PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    cpp = Path(temp) / 'touch.cpp'
    binary = Path(temp) / 'touch-test'
    cpp.write_text(prefix + core + '\n}\n' + getters + fixture)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra',
                    '-Wno-unused-variable', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I'+str(ROOT/'sdk/driver'),
                    '-I'+str(ROOT/'src/native'), str(cpp), '-o', str(binary)], check=True)
    for args in ([str(binary)], [str(binary), 'partial-failure']):
        subprocess.run(args, check=True)

# Connect the same production consumer to the actual C driver, with only the
# physical bus/clock/RTOS replaced. This catches contract drift between layers.
bus_fixture = (ROOT/'test/drivers/gt911_touch_test.c').read_text().split('int main(void)')[0]
integration = r'''
int main() {
  const auto* driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
  assert(driver->start(dependencies,2));
  api=static_cast<const risc_touch_api_v1*>(driver->capability);
  subscription=api->subscribe(nullptr); assert(subscription);
  assert(resync(true));
  NativeTouchPoint p{};
  for (unsigned tap=0; tap<100; ++tap) {
    nowMs+=10; fake_ms=nowMs;
    report_one(3,100,200);
    fail_point_reads=3;
    for (unsigned retry=0; retry<3; ++retry) {
      serviceProvider(); nowMs+=5; fake_ms=nowMs;
    }
    serviceProvider();
    report_release(); fail_ack_writes=1; failed_ack_reaches_controller=(tap%2)!=0;
    serviceProvider(); nowMs+=5; fake_ms=nowMs; serviceProvider();
    // Delay UI delivery across several complete taps, as during a refresh.
    if (tap%8==7 || tap==99) {
      unsigned count=(tap==99 ? 4 : 8);
      for (unsigned i=0;i<count;++i) assert(nativeTouchGetTap(p));
      assert(!nativeTouchGetTap(p));
    }
  }
  assert(nativeTouchDiagnostics().taps==100);
  assert(nativeTouchDiagnostics().tapOverflows==0);
  // Reproduce a menu tap still unread in the real GT911 provider at ELF entry.
  report_one(3,100,200);assert(api->poll(nullptr,1));
  report_release();assert(api->poll(nullptr,1));
  nativeTouchDiscardGestures();serviceProvider();assert(!nativeTouchGetTap(p));
  report_one(3,100,200);serviceProvider();report_release();serviceProvider();assert(nativeTouchGetTap(p));
  // A held launch contact crossing focus must lift, then a fresh contact works.
  report_one(3,100,200);serviceProvider();nativeTouchDiscardGestures();serviceProvider();
  report_release();serviceProvider();assert(!nativeTouchGetTap(p));
  report_one(3,100,200);serviceProvider();report_release();serviceProvider();assert(nativeTouchGetTap(p));

  // Real GT911 report/consumer queue under a 30-second consumer stall.
  for (unsigned tap=0;tap<12;++tap) {
    nowMs+=1000;fake_ms=nowMs;report_one(3,100,200);serviceProvider();
    nowMs+=30;fake_ms=nowMs;report_release();serviceProvider();
  }
  nowMs+=18000;fake_ms=nowMs;
  assert(!nativeTouchGetTap(p));
  nativeTouchBeginSurfaceTransition();serviceProvider();
  const auto epoch=nativeTouchPresentationEpoch();
  report_one(3,100,200);serviceProvider();
  nowMs+=25000;fake_ms=nowMs;
  nativeTouchSurfacePresented(epoch,false);assert(!nativeTouchGetContact(p));
  nativeTouchSurfacePresented(epoch,true);serviceProvider();
  report_release();serviceProvider();assert(!nativeTouchGetTap(p));
  report_one(3,100,200);serviceProvider();report_release();serviceProvider();assert(nativeTouchGetTap(p));
  assert(api->unsubscribe(nullptr,subscription));
  assert(driver->quiesce()); driver->stop();
  puts("actual GT911 + consumer: 100 taps, retry faults, delayed delivery and app focus fences PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp)
    cpp=temp/'integration.cpp'; obj=temp/'driver.o'; binary=temp/'test'
    cpp.write_text(prefix+core+'\n}\n'+getters+bus_fixture+integration)
    flags=['-D_POSIX_C_SOURCE=200809L','-pthread','-fsanitize=address,undefined',
           '-fno-omit-frame-pointer','-I'+str(ROOT/'sdk/driver'),
           '-I'+str(ROOT/'test/drivers/stub_idf_i2c')]
    subprocess.run(['cc','-std=c11',*flags,'-c',str(ROOT/'Drivers/gt911_touch/driver.c'),
                    '-o',str(obj)],check=True)
    subprocess.run(['c++','-std=c++17',*flags,'-I'+str(ROOT/'src/native'),
                    str(cpp),str(obj),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

# Compile complete production lifecycle code, rather than sliced gesture code,
# to exercise retained leases/subscriptions and RTOS stop-before-release rules.
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp/'freertos').mkdir()
    for header in ('Arduino.h', 'HalStorage.h', 'Logging.h',
                   'freertos/FreeRTOS.h', 'freertos/task.h'):
        (temp/header).write_text('#pragma once\n')
    binary = temp/'touch-lifetime-test'
    for board in ('BOARD_T5S3_PRO', 'BOARD_XTEINK_X4_PRO'):
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-D'+board, '-I'+str(temp), '-I'+str(ROOT/'sdk/driver'),
                        '-I'+str(ROOT/'src'),
                        str(ROOT/'test/drivers/native_touch_input_test.cpp'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
