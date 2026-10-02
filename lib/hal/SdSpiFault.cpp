#include "SdSpiFault.h"

#include "RuntimeFaultRetention.h"
#if defined(RISCRTE_SD_SPI_FAULT_PORT) || defined(RISCRTE_SD_SPI_FAULT_TEST)
#include <esp_rom_sys.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>

namespace {
// These observe the existing task/bus ownership; they are not another bus lock.
constexpr uint32_t kOperationMs = 30000, kTransferMs = 1000;
constexpr size_t kOwners = 64;
struct Owner {
  TaskHandle_t task{};
  TickType_t begun{}, yielded{};
  uint32_t depth{};
  bool retained{};
};
Owner owners[kOwners];
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> failed{false}, retainAllTasks{false};
Owner* find(TaskHandle_t task) {
  for (auto& owner : owners)
    if (owner.task == task) return &owner;
  return nullptr;
}
bool expired(TickType_t began, uint32_t ms) {
  return static_cast<TickType_t>(xTaskGetTickCount() - began) >= pdMS_TO_TICKS(ms);
}
void retainCurrent() {
  const auto task = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&stateLock);
  auto* owner = find(task);
  if (!owner)
    for (auto& candidate : owners)
      if (!candidate.task) {
        owner = &candidate;
        owner->task = task;
        break;
      }
  if (owner)
    owner->retained = true;
  else
    retainAllTasks.store(true, std::memory_order_release);
  portEXIT_CRITICAL(&stateLock);
}
[[noreturn]] void park() {
  retainCurrent();
  // Keep the TCB, stack, held mutexes and caller-owned buffers/mappings alive.
  // A parked task must not cause the watchdog to reboot the device implicitly.
  const bool unwatched = esp_task_wdt_status(nullptr) != ESP_OK || esp_task_wdt_delete(nullptr) == ESP_OK;
  for (;;) {
    if (unwatched) {
      vTaskSuspend(nullptr);  // An accidental resume cannot return to I/O.
    } else {
      // Failed watchdog removal is not permission to trigger an automatic reset.
      // Retain the same frame/resources while cooperatively feeding it instead.
      (void)esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}
}  // namespace
extern "C" bool risc_sd_spi_faulted() { return failed.load(std::memory_order_acquire); }
extern "C" void risc_sd_spi_fail(const char* reason) {
  retainCurrent();  // Publish deletion protection before the global fault.
  if (!failed.exchange(true, std::memory_order_acq_rel))
    esp_rom_printf("SD/SPI unavailable: %s; resources retained; MANUAL REBOOT REQUIRED\n", reason);
  park();
}
extern "C" bool risc_runtime_retention_required() { return risc_sd_spi_faulted(); }
extern "C" void risc_runtime_retention_guard() {
  if (risc_sd_spi_faulted()) park();
}
extern "C" void risc_sd_spi_guard() {
  if (risc_sd_spi_faulted()) park();
  const auto task = xTaskGetCurrentTaskHandle();
  bool timeout = false, cooperate = false;
  TickType_t begun = 0;
  portENTER_CRITICAL(&stateLock);
  auto* owner = find(task);
  if (owner && owner->depth) {
    begun = owner->begun;
    timeout = expired(begun, kOperationMs);
    cooperate = expired(owner->yielded, 8);
    if (cooperate) owner->yielded = xTaskGetTickCount();
  }
  portEXIT_CRITICAL(&stateLock);
  if (timeout) risc_sd_spi_fail("whole operation deadline");
  if (cooperate) {
    vTaskDelay(1);
    if (risc_sd_spi_faulted()) park();
    if (expired(begun, kOperationMs)) risc_sd_spi_fail("whole operation deadline");
  }
}
extern "C" void risc_sd_spi_begin_operation() {
  risc_sd_spi_guard();
  const auto task = xTaskGetCurrentTaskHandle();
  bool exhausted = false;
  portENTER_CRITICAL(&stateLock);
  auto* owner = find(task);
  if (!owner)
    for (auto& candidate : owners)
      if (!candidate.task) {
        owner = &candidate;
        owner->task = task;
        break;
      }
  if (!owner || owner->depth == UINT32_MAX)
    exhausted = true;
  else {
    if (!owner->depth) owner->begun = owner->yielded = xTaskGetTickCount();
    ++owner->depth;
  }
  portEXIT_CRITICAL(&stateLock);
  if (exhausted) risc_sd_spi_fail("operation ownership capacity");
}
extern "C" void risc_sd_spi_end_operation() {
  risc_sd_spi_guard();  // Never unwind or release a deadline-expired owner.
  portENTER_CRITICAL(&stateLock);
  auto* owner = find(xTaskGetCurrentTaskHandle());
  if (owner && owner->depth && !--owner->depth) *owner = {};
  portEXIT_CRITICAL(&stateLock);
}
extern "C" void risc_sd_spi_busy(uint32_t begun) {
  risc_sd_spi_guard();
  if (expired(begun, kTransferMs)) risc_sd_spi_fail("controller transfer did not stop");
  // Fast normal FIFO transfers do not acquire a millisecond delay.
  if (expired(begun, 8)) vTaskDelay(1);
}
extern "C" void risc_sd_spi_wait_lock(void* mutex) {
  risc_sd_spi_guard();
  if (!mutex) risc_sd_spi_fail("missing existing mutex");
  const auto begun = xTaskGetTickCount();
  // Bound the whole acquisition, not each retry. Waiting never touches the bus
  // or gives a mutex it did not acquire, and registered callers remain fed.
  for (unsigned attempts = 0; attempts < 300; ++attempts) {
    risc_sd_spi_guard();
    if (expired(begun, kOperationMs)) break;
    if (esp_task_wdt_status(nullptr) == ESP_OK) (void)esp_task_wdt_reset();
    if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex), pdMS_TO_TICKS(100)) == pdTRUE) {
      risc_sd_spi_guard();
      return;
    }
  }
  risc_sd_spi_fail("existing owner did not release mutex");
}
extern "C" void __real_vTaskDelete(TaskHandle_t task);
extern "C" void __wrap_vTaskDelete(TaskHandle_t task) {
  const auto target = task ? task : xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&stateLock);
  const auto* owner = find(target);
  const bool retained = retainAllTasks.load(std::memory_order_acquire) || (owner && (owner->retained || owner->depth));
  portEXIT_CRITICAL(&stateLock);
  // A void deletion API cannot truthfully report failure. Retain its caller too
  // rather than let it free another task's still-live stack/resources afterward.
  if (retained) risc_sd_spi_fail("task disposal before port quiescence");
  __real_vTaskDelete(task);
}
#endif
