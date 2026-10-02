#include "ActivityManager.h"

#include <HalPowerManager.h>

#include "CrossPointSettings.h"
#include "GlobalMenuActivity.h"
#include "OpdsServerStore.h"
#include "components/StartupScreen.h"
#include "native/NativeSerialPortBridge.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "home/CrashActivity.h"
#include "home/HomeActivity.h"
#if defined(BOARD_XTEINK_X4_PRO)
#include "home/X4TxtActivity.h"
#endif
#include "home/RecentBooksActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/FullScreenMessageActivity.h"
#include "util/InstalledAppActivity.h"

namespace {
constexpr DisplayPresentMode kUiPageTransitionRefreshMode = DisplayPresentMode::Quality;
}  // namespace

void ActivityManager::begin() {
  xTaskCreate(&renderTaskTrampoline, "ActivityManagerRender",
              12288, this, 1, &renderTaskHandle);
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    RenderLock lock;
    if (currentActivity) {
#if defined(BOARD_XTEINK_X4_PRO)
      // X4 first Home frame uses the already attached display provider. Its
      // power module and T5S3 StartupScreen are not part of this bootstrap.
      currentActivity->render(std::move(lock));
#else
      HalPowerManager::Lock powerLock;
      // Readiness is the destination's first render after startup/onEnter.
      // Keep the loading worker alive throughout Home or reader preparation.
      if (currentActivity->name != "Boot" && !StartupScreen::finishBoot(renderer)) {
        xTaskNotify(renderTaskHandle, 1, eIncrement);
        delay(1);
        continue;
      }
      currentActivity->render(std::move(lock));
      if (currentActivity->name != "Boot") StartupScreen::destinationReady();
#endif
    }
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&waitingTaskMux);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&waitingTaskMux);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  // MappedInputManager::update() already performs the owner-task discovery
  // service immediately before this loop. Do not repeat provider/device work
  // before dispatching captured input.
  bool injectedTouchButtonTap = false;
  if (currentActivity && !StartupScreen::isLoading()) {
    bool activityHandled = false;
    const bool globalMenuAllowed = currentActivity->supportsGlobalMenu();

    MappedInputManager::TouchPoint swipeStart{}, swipeEnd{};
    if (mappedInput.getTouchSwipe(swipeStart, swipeEnd, renderer)) {
      constexpr int kTopBand = 60;
      constexpr int kDragMin = 70;
      if (globalMenuAllowed && swipeStart.y < kTopBand && (swipeEnd.y - swipeStart.y) >= kDragMin) {
        openGlobalMenu();
        activityHandled = true;
      } else {
        activityHandled = currentActivity->onTouchSwipe(swipeStart.x, swipeStart.y, swipeEnd.x, swipeEnd.y);
      }
    }

    const auto doSingleHome = [this] {
      if (currentActivity && currentActivity->supportsTouchHomeButton() && currentActivity->name != "Home") {
        currentActivity->onGoHome();
      }
    };
    if (!activityHandled && mappedInput.wasTouchHomeButtonPressed()) {
      if (currentActivity->onTouchHomeButton()) {
      } else if (globalMenuAllowed && SETTINGS.doubleClickHomeMenu) {
        if (pendingHomeSingle && millis() - lastHomeEventMs <= kDoubleClickWindowMs) {
          pendingHomeSingle = false;
          openGlobalMenu();
        } else {
          pendingHomeSingle = true;
          lastHomeEventMs = millis();
        }
      } else if (currentActivity->supportsTouchHomeButton() && currentActivity->name != "Home") {
        currentActivity->onGoHome();
      }
      activityHandled = true;
    }
    if (pendingHomeSingle && millis() - lastHomeEventMs > kDoubleClickWindowMs) {
      pendingHomeSingle = false;
      doSingleHome();
      activityHandled = true;
    }

    MappedInputManager::TouchPoint touchPoint{};
    bool touchHandled = false;
    if (!activityHandled && mappedInput.wasTouchTapped(touchPoint, renderer)) {
      if (currentActivity->showsHomeTouchButton() && currentActivity->isHomeTouchTap(touchPoint.x, touchPoint.y)) {
        currentActivity->onGoHome();
        touchHandled = true;
      } else {
        MappedInputManager::Button touchButton;
        if (currentActivity->resolveTouchButtonHint(touchPoint.x, touchPoint.y, touchButton)) {
          mappedInput.injectButtonTap(touchButton);
          injectedTouchButtonTap = true;
        } else {
          touchHandled = currentActivity->onTouchTap(touchPoint.x, touchPoint.y);
        }
      }
    }
    if (!activityHandled && !touchHandled) {
      currentActivity->loop();
    }
  }

  if (injectedTouchButtonTap) {
    mappedInput.clearInjectedButtonTap();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();
        goHome();
        continue;
      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        renderer.requestNextRefresh(DisplayPresentMode::Quality);
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();
          handler(pendingResult);
        }
        if (pendingAction == PendingAction::None) {
          requestUpdate();
        }
        continue;
      }
    } else if (pendingActivity) {
      RenderLock lock;
      if (pendingAction == PendingAction::Replace) {
        exitActivity(lock);
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      const auto transitionAction = pendingAction;
      const auto replaceRefreshMode = pendingReplaceRefreshMode;
      pendingAction = PendingAction::None;
      pendingReplaceRefreshMode = DisplayPresentMode::Clean;
      currentActivity = std::move(pendingActivity);
      renderer.requestNextRefresh(transitionAction == PendingAction::Replace ? replaceRefreshMode
                                                                             : DisplayPresentMode::Quality);
      lock.unlock();
      currentActivity->onEnter();
      continue;
    }
  }

  if (requestedUpdate) {
    requestedUpdate = false;
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  replaceActivity(std::move(newActivity), DisplayPresentMode::Clean);
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity,
                                      const DisplayPresentMode replaceRefreshMode) {
  pendingReplaceRefreshMode = replaceRefreshMode;
  if (currentActivity) {
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer() {
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
}

void ActivityManager::goToSettings() {
  replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
}

void ActivityManager::goToInstalledApp(std::string artifact, std::string displayName) {
  replaceActivity(std::make_unique<InstalledAppActivity>(
      renderer, mappedInput, std::move(artifact), std::move(displayName)),
      kUiPageTransitionRefreshMode);
}

void ActivityManager::goToRecentBooks() {
#if defined(BOARD_XTEINK_X4_PRO)
  replaceActivity(std::make_unique<X4TxtActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
#else
  replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
#endif
}

void ActivityManager::goToBrowser() {
  const auto& servers = OPDS_STORE.getServers();
  if (servers.size() == 1) {
    replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]));
  } else {
    replaceActivity(std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true));
  }
}

void ActivityManager::goToReader(std::string path, const DisplayPresentMode replaceRefreshMode) {
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), replaceRefreshMode),
                  replaceRefreshMode);
}

void ActivityManager::goToSleep(bool poweringOff) {
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, poweringOff));
  loop();
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

void ActivityManager::goHome() {
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
}

void ActivityManager::openGlobalMenu() {
  if (!currentActivity || !currentActivity->supportsGlobalMenu() || pendingActivity) {
    return;
  }
  const bool overReader = currentActivity->isReaderActivity();
  pushActivity(std::make_unique<GlobalMenuActivity>(renderer, mappedInput, overReader));
}

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (pendingActivity) {
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const { return currentActivity && currentActivity->isReaderActivity(); }

bool ActivityManager::isReaderActivityInStack() const {
  if (currentActivity && currentActivity->isReaderActivity()) return true;
  for (const auto& act : stackActivities) {
    if (act && act->isReaderActivity()) return true;
  }
  return false;
}

bool ActivityManager::isReaderPageActivity() const {
  return currentActivity && (currentActivity->name == "EpubReader" || currentActivity->name == "TxtReader" ||
                             currentActivity->name == "XtcReader");
}

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    requestedUpdate = true;
  }
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }
  taskENTER_CRITICAL(&waitingTaskMux);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&waitingTaskMux);
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");
  assert(!alreadyWaiting && "Already waiting for a render to complete");
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");
  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
  ownerTask = xTaskGetCurrentTaskHandle();
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
  ownerTask = xTaskGetCurrentTaskHandle();
}

RenderLock::~RenderLock() { unlock(); }

void RenderLock::unlock() {
  if (!isLocked) {
    return;
  }
  TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
  TaskHandle_t holderTask = xSemaphoreGetMutexHolder(activityManager.renderingMutex);
  if (ownerTask == nullptr) {
    ownerTask = holderTask;
  }
  if (ownerTask == nullptr || holderTask != ownerTask || currentTask != ownerTask) {
    LOG_ERR("ACT", "RenderLock unlock skipped: owner=%p holder=%p current=%p", ownerTask, holderTask, currentTask);
    isLocked = false;
    ownerTask = nullptr;
    return;
  }
  xSemaphoreGive(activityManager.renderingMutex);
  isLocked = false;
  ownerTask = nullptr;
}

bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };
