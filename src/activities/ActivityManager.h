#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "util/ScreenshotInfo.h"

class Activity;
class RenderLock;

class ActivityManager {
  friend class RenderLock;

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;

  void exitActivity(const RenderLock& lock);

  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;
  HalDisplay::RefreshMode pendingReplaceRefreshMode = HalDisplay::FULL_REFRESH;

  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();

  TaskHandle_t waitingTaskHandle = nullptr;
  portMUX_TYPE waitingTaskMux = portMUX_INITIALIZER_UNLOCKED;

  SemaphoreHandle_t renderingMutex = nullptr;

  bool requestedUpdate = false;

  static constexpr unsigned long kDoubleClickWindowMs = 400;
  unsigned long lastHomeEventMs = 0;
  bool pendingHomeSingle = false;

 public:
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~ActivityManager();

  void begin();
  void loop();

  void replaceActivity(std::unique_ptr<Activity>&& newActivity);
  void replaceActivity(std::unique_ptr<Activity>&& newActivity, HalDisplay::RefreshMode replaceRefreshMode);

  void goToFileTransfer();
  void goToSettings();
  void goToFileBrowser(std::string path = {});
  void goToRecentBooks();
  void goToBrowser();
  void goToLlmChat();
  void goToTimecard();
  void goToReader(std::string path, HalDisplay::RefreshMode replaceRefreshMode = HalDisplay::HALF_REFRESH);
  void goToSleep(bool poweringOff = false);
  void goToBoot();
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
  void goHome();

  void openGlobalMenu();
  void pushActivity(std::unique_ptr<Activity>&& activity);
  void popActivity();

  bool preventAutoSleep() const;
  bool isReaderActivity() const;
  bool isReaderActivityInStack() const;
  bool isReaderPageActivity() const;
  bool skipLoopDelay() const;
  ScreenshotInfo getScreenshotInfo() const;

  // Native ELF UI bridges execute synchronously while ActivityManager is paused
  // and the normal RenderLock is held by runNativeApp(). Keep access narrow and
  // use these only for firmware-owned native UI services.
  GfxRenderer& nativeAppRenderer() { return renderer; }
  MappedInputManager& nativeAppInput() { return mappedInput; }

  void requestUpdate(bool immediate = false);
  void requestUpdateAndWait();
};

extern ActivityManager activityManager;
