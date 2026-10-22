// Host-only side-effect recorder for the extracted, unmodified production entrypoints.
#include "runtime/power/SleepOrchestration.h"
#include <cstdio>
#include <string>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
static std::string calls;
static unsigned flags;
static bool flag(unsigned n) { return flags & (1u << n); }
static void call(const char* name) { calls += name; calls += ','; }
namespace NativeReaderEntry {
enum class Action { Sleep, SleepKeepingScreen, PowerOff };
bool deferSleep(Action a, bool touch = true) {
  call(a == Action::Sleep ? "defer-sleep" : a == Action::PowerOff ? "defer-off" : touch ? "defer-keep-touch" : "defer-keep-button");
  return flag(0);
}
}
namespace CrossPointSettings { constexpr int DIGITAL_CLOCK = 1; }
struct Settings { int sleepScreen = 0; int backlightLevel = 42; bool resumeReaderOnBoot = false; } SETTINGS;
struct State {
  bool lastSleepFromReader = false;
  std::string openEpubPath;
  void saveToFile() { call(lastSleepFromReader ? "save-reader" : "save-home"); }
} APP_STATE;
struct Renderer {
  void requestNextRefresh(int) { call("refresh"); }
} renderer;
struct ActivityManager {
  bool isReaderActivityInStack() { call("reader-query"); return flag(4); }
  void goToSleep(bool off = false) { call(off ? "screen-off" : "screen-sleep"); }
  void goToReader(const std::string&, int) { call("reader"); }
  void goHome() { call("home"); }
} activityManager;
struct HalDisplay { static constexpr int FULL_REFRESH = 1; };
struct Display { bool deepSleep() { call("display-barrier"); return flag(6); } } display;
struct Tilt { void deepSleep() { call("tilt"); } } halTiltSensor;
struct HalPowerManager {
  struct Lock { Lock() { call("lock"); } ~Lock() { call("unlock"); } };
} ;
struct Gpio {} gpio;
struct PowerManager {
  void startDeepSleep(Gpio&, bool touch = true) { call(touch ? "terminal-touch" : "terminal-button"); }
} powerManager;
namespace Board {
struct Capabilities { bool hasHardPowerOff; };
Capabilities capabilities() { call("capabilities"); return {flag(10)}; }
bool shutdownBatteryPower() { call("shutdown"); return flag(11); }
void setBacklightLevel(int level) { call(level == 0 ? "light-off" : "light-on"); }
void restoreBacklightLevel(int) { call("light-restore"); }
}
namespace BoardPowerPort {
bool prepareShutdown() { call("reserve"); return flag(7); }
bool cancelShutdown() { call("cancel"); return flag(8); }
}
bool drainPlatformProvidersForSleep() { call("drain"); return flag(9); }
bool suspendInputProvidersForSleep() { call("suspend"); return flag(3); }
void resumeInputProvidersAfterSleep() { call("resume"); }
bool shouldSuppressDeepSleepForDebug() { call("debug"); return flag(1); }
void waitForPowerRelease() { call("release-button"); }
void delay(unsigned ms) { call(ms == 1500 ? "wait-shutdown" : "unexpected-delay"); }
namespace DeskClockSleep { void run(Renderer&, Gpio&) { call("clock"); } }
