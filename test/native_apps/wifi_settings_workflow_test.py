#!/usr/bin/env python3
"""Compile the real Wi-Fi activity against deterministic activity/ELF fixtures.

This exercises wrapper state and deferred navigation, not radio/device I/O.
WIFI_ACTIVITY_SOURCE_ROOT allows original-fails comparison with a prior tree.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("WIFI_ACTIVITY_SOURCE_ROOT", ROOT))

COMMON = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
enum class DisplayPresentMode { Balanced };
struct RenderLock {};
struct Rect { int x, y, w, h; };
struct GfxRenderer {
  void clearScreen() {}
  int getScreenWidth() { return 480; }
  int getScreenHeight() { return 800; }
  void drawCenteredText(int, int, const char*) {}
  void displayBuffer(DisplayPresentMode) {}
};
struct MappedInputManager {
  enum class Button { Back, Confirm };
  bool back = false, confirm = false;
  bool wasPressed(Button b) { return b == Button::Back ? back : confirm; }
  struct Labels { const char *btn1, *btn2, *btn3, *btn4; };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) {
    return {a,b,c,d};
  }
};
struct ActivityResult { bool isCancelled = false; };
inline bool pendingHandoff = false;
class Activity {
 public:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  bool finished = false;
  unsigned updates = 0;
  std::unique_ptr<Activity> child;
  std::function<void(const ActivityResult&)> completion;
  Activity(const char*, GfxRenderer& r, MappedInputManager& i) : renderer(r), mappedInput(i) {}
  virtual ~Activity() = default;
  virtual void onEnter() { finished = false; }
  virtual void loop() {}
  virtual bool onTouchTap(int16_t,int16_t) { return false; }
  virtual void render(RenderLock&&) {}
  void requestUpdate() { ++updates; }
  void startActivityForResult(std::unique_ptr<Activity>&& c,
                             std::function<void(const ActivityResult&)> f) {
    assert(!child); child = std::move(c); completion = std::move(f);
  }
  void completeChild(bool cancelled) {
    assert(child); child.reset(); auto f = std::move(completion); f({cancelled});
  }
  void finish() { finished = true; pendingHandoff = false; }
  bool resolveTouchButtonHint(int16_t x,int16_t, MappedInputManager::Button& b) {
    if (x < 0) return false;
    b = x == 0 ? MappedInputManager::Button::Back : MappedInputManager::Button::Confirm;
    return true;
  }
};
inline bool deferEntry=false;
struct ActivityManager {bool deferNativeAppLoop(Activity*){return deferEntry;}}; inline ActivityManager activityManager;
class RequiredAppActivity : public Activity {
 public:
  std::string artifact, label;
  RequiredAppActivity(GfxRenderer& r, MappedInputManager& i, const char* a, const char* l)
    : Activity("required",r,i), artifact(a), label(l) {}
};
inline bool installed = true, queueHandoff = false;
inline unsigned resolves = 0, launches = 0;
inline std::string launchedPath;
using esp_err_t = int;
constexpr int ESP_OK = 0;
inline int launchResult = ESP_OK;
inline bool resolveInstalledAppPath(const char* name, std::string& path) {
  assert(std::string(name) == "wifi_settings.elf"); ++resolves;
  if (!installed) return false;
  path = "/sd/Apps/wifi_settings/wifi_settings.elf"; return true;
}
inline int runNativeApp(const char* path, GfxRenderer&, MappedInputManager&) {
  ++launches; launchedPath = path; pendingHandoff = queueHandoff; return launchResult;
}
struct Metrics { int topPadding=1, headerHeight=2, verticalSpacing=3; };
class UITheme {
 public:
  static UITheme& getInstance() { static UITheme t; return t; }
  const Metrics& getMetrics() { static Metrics m; return m; }
};
struct GUIType {
  void drawHeader(GfxRenderer&,Rect,const char*) {}
  void drawButtonHints(GfxRenderer&,const char*,const char*,const char*,const char*) {}
};
inline GUIType GUI;
constexpr int UI_10_FONT_ID=10, SMALL_FONT_ID=1, STR_BACK=0;
inline const char* tr(int) { return "Back"; }
'''

CASES = r'''
#include "activities/settings/WifiSettingsActivity.h"
#include <iostream>
void reset() {
  installed=true; queueHandoff=false; pendingHandoff=false; deferEntry=false;
  resolves=launches=0; launchedPath.clear(); launchResult=ESP_OK;
}
int main() {
  GfxRenderer r; MappedInputManager input;
  reset(); WifiSettingsActivity a(r,input); a.onEnter(); a.loop();
  assert(resolves==1 && launches==1);
  assert(launchedPath=="/sd/Apps/wifi_settings/wifi_settings.elf");
  assert(!a.finished); a.loop(); assert(a.finished && launches==1);
  reset(); deferEntry=true; a.onEnter(); a.loop();
  assert(!resolves&&!launches&&!a.finished&&!a.child);
  deferEntry=false; a.loop(); assert(resolves==1&&launches==1);
  a.loop(); assert(a.finished);
  // Re-entry starts a fresh resolution instead of using a cached generation.
  a.onEnter(); a.loop(); assert(resolves==2 && launches==2 && !a.finished);
  a.loop(); assert(a.finished);

  reset(); installed=false; a.onEnter(); a.loop();
  assert(a.child && launches==0 && !a.finished);
  auto* required = dynamic_cast<RequiredAppActivity*>(a.child.get());
  assert(required && required->artifact=="wifi_settings.elf" && required->label=="Wi-Fi Networks");
  a.completeChild(true); assert(!a.finished); a.loop();
  assert(a.finished && !a.child && launches==0 && resolves==1);

  reset(); installed=false; a.onEnter(); a.loop();
  installed=true; a.completeChild(false); a.loop();
  assert(!a.child && launches==1 && resolves==2 && !a.finished);
  a.loop(); assert(a.finished);

  // A still-missing package is re-resolved; it is never launched by loose path.
  reset(); installed=false; a.onEnter(); a.loop(); a.completeChild(false); a.loop();
  assert(a.child && resolves==2 && launches==0); a.completeChild(true); a.loop();
  assert(a.finished);

  reset(); queueHandoff=true; a.onEnter(); a.loop();
  assert(pendingHandoff && !a.finished); // same manager iteration retains child
  pendingHandoff=false; a.loop(); assert(a.finished); // resume after child unwinds

  reset(); launchResult=7; a.onEnter(); a.loop();
  assert(!a.finished); a.loop(); assert(!a.finished);
  input.back=true; a.loop(); assert(a.finished); input.back=false;
  a.onEnter(); a.loop(); input.confirm=true; a.loop(); assert(a.finished); input.confirm=false;
  a.onEnter(); a.loop(); assert(!a.onTouchTap(-1,0) && !a.finished);
  assert(a.onTouchTap(0,0) && a.finished);
  a.render(RenderLock{});
  std::cout << "Wi-Fi Settings workflow: 8 state/handoff cases passed\n";
}
'''

with tempfile.TemporaryDirectory() as temp:
    tmp = Path(temp)
    (tmp / "fixture.h").write_text(COMMON)
    for name in ("GfxRenderer.h", "I18n.h", "MappedInputManager.h", "fontIds.h",
                 "activities/Activity.h", "activities/util/RequiredAppActivity.h",
                 "components/UITheme.h", "native/InstalledAppPath.h", "native/NativeAppHost.h"):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "fixture.h"\n')
    (tmp / "cases.cpp").write_text(CASES)
    exe = tmp / "wifi-workflow"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(tmp),
                    "-I" + str(SOURCE / "src"), str(tmp / "cases.cpp"),
                    str(SOURCE / "src/activities/settings/WifiSettingsActivity.cpp"),
                    "-o", str(exe)], check=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=30)
