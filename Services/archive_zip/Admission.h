#pragma once
// Provider-local synchronization uses the already admitted generic OS mutex
// primitives on target. Do not reintroduce relocatable target atomics: the I2C
// provider's recorded0.1.3/0.1.4 regression showed that is not a safe precedent.
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
class ArchiveAdmission {
  SemaphoreHandle_t mutex_ = nullptr, stop_ = nullptr;
 public:
  bool prepare() {
    if (mutex_ && stop_) return true;
    mutex_ = xSemaphoreCreateMutex(); stop_ = xSemaphoreCreateBinary();
    if (!mutex_ || !stop_) { destroy(); return false; }
    return true;
  }
  bool ready() const { return mutex_ && stop_; }
  bool enter() { return mutex_ && xSemaphoreTake(mutex_, 0) == pdTRUE; }
  void leave() { (void)xSemaphoreGive(mutex_); }
  bool stopping() const { return !stop_ || uxSemaphoreGetCount(stop_) != 0; }
  void requestStop() { if (stop_) (void)xSemaphoreGive(stop_); }
  void restart() { if (stop_) (void)xSemaphoreTake(stop_, 0); }
  void destroy() {
    if (stop_) vSemaphoreDelete(stop_);
    if (mutex_) vSemaphoreDelete(mutex_);
    stop_ = mutex_ = nullptr;
  }
};
#else
#include <atomic>
#include <mutex>
class ArchiveAdmission {
  std::mutex mutex_;
  std::atomic<bool> stopped_{false};
  bool ready_ = false;
 public:
  bool prepare() { ready_ = true; return true; }
  bool ready() const { return ready_; }
  bool enter() { return ready_ && mutex_.try_lock(); }
  void leave() { mutex_.unlock(); }
  bool stopping() const { return stopped_.load(); }
  void requestStop() { stopped_.store(true); }
  void restart() { stopped_.store(false); }
  void destroy() { ready_ = false; }
};
#endif
