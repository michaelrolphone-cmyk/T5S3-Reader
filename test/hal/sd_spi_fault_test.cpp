#include "SdSpiFault.h"
#include "RuntimeFaultRetention.h"
#include <freertos/FreeRTOS.h>
#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstring>
static uint32_t now, delayed, takes, deletes, watchdogRemoved, notices, suspended;
static TaskHandle_t task = reinterpret_cast<void*>(1);
static bool takeOK = true, advancing, failWatchdogDelete;
static std::jmp_buf parked;
TickType_t xTaskGetTickCount() { if (advancing) ++now; return now; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return task; }
void vTaskDelay(TickType_t n) { now += n; ++delayed; if (failWatchdogDelete && n==100) { ++suspended; std::longjmp(parked,1); } }
void vTaskSuspend(TaskHandle_t t) { assert(!t); ++suspended; std::longjmp(parked, 1); }
int xSemaphoreTake(SemaphoreHandle_t m, TickType_t n) { assert(m && n==100); ++takes; if (!takeOK) now+=n; return takeOK; }
int esp_task_wdt_status(void*) { return 0; }
int esp_task_wdt_reset() { return 0; }
int esp_task_wdt_delete(void*) { ++watchdogRemoved; return failWatchdogDelete ? -1 : 0; }
int esp_rom_printf(const char*, ...) { ++notices; return 0; }
extern "C" void __real_vTaskDelete(TaskHandle_t) { ++deletes; }
extern "C" void __wrap_vTaskDelete(TaskHandle_t);
#ifdef RISC_ACTUAL_SPI_WAIT
#include "actual_spi_wait.inc"
#endif
int main(int argc, char** argv) {
  assert(argc==2 && !risc_sd_spi_faulted());
  const char* mode=argv[1];
  failWatchdogDelete=!strcmp(mode,"wdt-failure");
  if (!strcmp(mode,"normal")) {
    risc_sd_spi_begin_operation(); now=10;
    risc_sd_spi_begin_operation(); risc_sd_spi_wait_lock(&now);
    risc_sd_spi_end_operation(); risc_sd_spi_busy(now); risc_sd_spi_end_operation();
    __wrap_vTaskDelete(nullptr);
    assert(takes==1 && deletes==1 && !suspended && !notices && !risc_runtime_retention_required());
    return 0;
  }
  if (!setjmp(parked)) {
    if (!strcmp(mode,"capacity")) {
      for(uintptr_t n=1;n<=65;++n) { task=reinterpret_cast<void*>(n); risc_sd_spi_begin_operation(); }
    } else {
      if (!strcmp(mode,"wrap")) now=UINT32_MAX-500;
      const uint32_t start=now;
      risc_sd_spi_begin_operation();
#ifdef RISC_ACTUAL_SPI_WAIT
      if (!strcmp(mode,"actual-wait")) { advancing=true; spi_dev_t device{}; device.cmd.usr=1; spi_t bus{&device,0}; spiWaitReady(&bus); }
      else
#endif
      if (!strcmp(mode,"delete-active")) { __wrap_vTaskDelete(task); }
      else if (!strcmp(mode,"lock")) { takeOK=false; risc_sd_spi_wait_lock(&now); }
      else if (!strcmp(mode,"operation")) {
        now+=29999; risc_sd_spi_begin_operation(); risc_sd_spi_end_operation();
        ++now; risc_sd_spi_guard(); // Nesting/sector retries did not reset start.
      } else {
        now+=8; risc_sd_spi_busy(start); assert(delayed>=1);
        now=start+1000; risc_sd_spi_busy(start);
      }
    }
    assert(false);
  }
  assert(risc_sd_spi_faulted() && risc_runtime_retention_required());
  assert(notices==1 && watchdogRemoved==1 && suspended==1 && !deletes);
  const auto original=task;
  task=reinterpret_cast<void*>(100);
  if (!setjmp(parked)) { __wrap_vTaskDelete(original); assert(false); }
  assert(!deletes && suspended==2 && notices==1);
  task=reinterpret_cast<void*>(101);
  if (!setjmp(parked)) { risc_runtime_retention_guard(); assert(false); }
  assert(suspended==3 && notices==1);
  // No reset/retry can restore readiness. A subsequent operation parks before
  // acquisition, independently of the simulated mutex becoming available.
  takeOK=true; const auto before=takes;
  task=reinterpret_cast<void*>(102);
  if (!setjmp(parked)) { risc_sd_spi_wait_lock(&now); assert(false); }
  assert(takes==before && notices==1 && watchdogRemoved==4);
  std::puts("SD/SPI permanent fault: bounded detection, retained tasks, no reacquire/reset PASS");
}
