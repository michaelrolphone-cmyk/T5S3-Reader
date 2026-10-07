#include "SdmmcBootstrapDisk.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ff.h"
#include "diskio_impl.h"

namespace BootstrapSdmmc {
namespace {
constexpr uint32_t kSectorBytes = 512;
constexpr uint32_t kMaxSectors = 8192;
constexpr uint32_t kMaxCommands = 16384;
constexpr int kCommandTimeoutMs = 250;
std::atomic<bool> claimed{false}, cancelled{false};
TaskHandle_t owner = nullptr;
sdmmc_card_t card{};
FATFS filesystem{};
BYTE disk = FF_DRV_NOT_USED;
char volume[4]{};
bool hostUp = false, diskRegistered = false, mounted = false, writes = false;
bool operation = false;
int64_t deadline = 0;
uint32_t remainingSectors = 0;
Progress progress{};
alignas(4) uint8_t bounce[kSectorBytes];

bool onOwner() { return owner && owner == xTaskGetCurrentTaskHandle(); }
bool checkpoint() {
  if (!onOwner() || !operation || cancelled.load() || esp_timer_get_time() >= deadline) {
    progress.error = ESP_ERR_TIMEOUT;
    return false;
  }
  return progress.error == ESP_OK;
}
esp_err_t transaction(int slot, sdmmc_command_t* command) {
  if (!checkpoint() || ++progress.commands > kMaxCommands) {
    progress.error = ESP_ERR_TIMEOUT;
    return progress.error;
  }
  const int64_t remainingMs = (deadline - esp_timer_get_time()) / 1000;
  if (remainingMs <= 0) return progress.error = ESP_ERR_TIMEOUT;
  const int cap = static_cast<int>(std::min<int64_t>(kCommandTimeoutMs, remainingMs));
  if (command->timeout_ms <= 0 || command->timeout_ms > cap) command->timeout_ms = cap;
  const esp_err_t result = sdmmc_host_do_transaction(slot, command);
  // Cooperate even on tiny reads or slow media. The underlying command has
  // its own timeout; this is not merely a deadline checked after unbounded I/O.
  vTaskDelay(1);
  // Card initialization deliberately probes unsupported commands. Do not
  // poison the operation for those protocol responses; sdmmc_card_init owns it.
  if (!checkpoint()) return progress.error;
  return result;
}
DSTATUS status(BYTE which) {
  if (!onOwner() || which != disk || !hostUp) return STA_NOINIT;
  return writes ? 0 : STA_PROTECT;
}
DRESULT transfer(BYTE which, BYTE* dst, const BYTE* src, DWORD sector, UINT count) {
  if (which != disk || !checkpoint() || !count || count > remainingSectors ||
      sector >= static_cast<uint32_t>(card.csd.capacity) ||
      count > static_cast<uint32_t>(card.csd.capacity) - sector) {
    progress.error = ESP_ERR_INVALID_SIZE;
    return RES_PARERR;
  }
  if (src && !writes) return RES_WRPRT;
  if ((!src && !dst) || count > kMaxSectors) return RES_PARERR;
  for (UINT i = 0; i < count; ++i) {
    if (!checkpoint()) return RES_ERROR;
    --remainingSectors;
    esp_err_t result;
    if (src) {
      std::memcpy(bounce, src + i * kSectorBytes, kSectorBytes);
      result = sdmmc_write_sectors(&card, bounce, sector + i, 1);
    } else {
      result = sdmmc_read_sectors(&card, bounce, sector + i, 1);
      if (result == ESP_OK) std::memcpy(dst + i * kSectorBytes, bounce, kSectorBytes);
    }
    ++progress.sectors;
    if (result != ESP_OK) { progress.error = result; return RES_ERROR; }
    vTaskDelay(1);
  }
  return checkpoint() ? RES_OK : RES_ERROR;
}
DRESULT readDisk(BYTE d, BYTE* p, DWORD s, UINT n) { return transfer(d, p, nullptr, s, n); }
DRESULT writeDisk(BYTE d, const BYTE* p, DWORD s, UINT n) { return transfer(d, nullptr, p, s, n); }
DRESULT control(BYTE which, BYTE command, void* value) {
  if (which != disk || !checkpoint()) return RES_NOTRDY;
  switch (command) {
    case CTRL_SYNC: return RES_OK; // Each sector operation is synchronous.
    case GET_SECTOR_COUNT:
      if (!value) return RES_PARERR;
      *static_cast<DWORD*>(value) = card.csd.capacity; return RES_OK;
    case GET_SECTOR_SIZE:
      if (!value) return RES_PARERR;
      *static_cast<WORD*>(value) = kSectorBytes; return RES_OK;
    case GET_BLOCK_SIZE:
      if (!value) return RES_PARERR;
      *static_cast<DWORD*>(value) = 1; return RES_OK;
    default: return RES_PARERR; // No trim/erase/format operation exposed.
  }
}
const ff_diskio_impl_t diskIo{status, status, readDisk, writeDisk, control};
}

bool beginOperation(uint32_t milliseconds, uint32_t maxSectors) {
  if (!onOwner() || operation || !milliseconds || milliseconds > 20000 ||
      !maxSectors || maxSectors > kMaxSectors) return false;
  cancelled.store(false);
  deadline = esp_timer_get_time() + static_cast<int64_t>(milliseconds) * 1000;
  remainingSectors = maxSectors;
  progress = {};
  operation = true;
  return true;
}
Progress endOperation() {
  if (!onOwner()) return Progress{0, 0, ESP_ERR_INVALID_STATE};
  operation = false;
  return progress;
}
const char* drive() { return onOwner() && mounted ? volume : nullptr; }
void cancel() { cancelled.store(true); }
bool retained() { return claimed.load(); }

esp_err_t unmount() {
  if (!onOwner()) return ESP_ERR_INVALID_STATE;
  // Caller closes all handles first. Never run a second physical owner when
  // deinitialization is uncertain: retain claimed state on a deinit failure.
  if (diskRegistered) {
    f_mount(nullptr, volume, 0);
    ff_diskio_unregister(disk);
    diskRegistered = mounted = false;
  }
  if (hostUp) {
    const esp_err_t result = sdmmc_host_deinit();
    if (result != ESP_OK) return result;
    hostUp = false;
  }
  disk = FF_DRV_NOT_USED;
  operation = false;
  owner = nullptr;
  claimed.store(false);
  return ESP_OK;
}
esp_err_t mount(Pins pins, bool writable) {
  bool expected = false;
  if (!claimed.compare_exchange_strong(expected, true)) return ESP_ERR_INVALID_STATE;
  owner = xTaskGetCurrentTaskHandle();
  writes = writable;
  card = {};
  if (!beginOperation(20000, 4096)) { unmount(); return ESP_ERR_INVALID_STATE; }
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  host.command_timeout_ms = kCommandTimeoutMs;
  host.do_transaction = transaction;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = static_cast<gpio_num_t>(pins.clk);
  slot.cmd = static_cast<gpio_num_t>(pins.cmd);
  slot.d0 = static_cast<gpio_num_t>(pins.d0);
  esp_err_t result = sdmmc_host_init();
  if (result == ESP_OK) {
    hostUp = true;
    result = sdmmc_host_init_slot(host.slot, &slot);
  }
  if (result == ESP_OK) result = sdmmc_card_init(&host, &card);
  if (result == ESP_OK && (card.csd.sector_size != kSectorBytes || card.csd.capacity <= 0))
    result = ESP_ERR_NOT_SUPPORTED;
  if (result == ESP_OK) result = ff_diskio_get_drive(&disk);
  if (result == ESP_OK && disk > 9) result = ESP_ERR_NOT_SUPPORTED;
  if (result == ESP_OK) {
    volume[0] = '0' + disk; volume[1] = ':'; volume[2] = 0;
    ff_diskio_register(disk, &diskIo);
    diskRegistered = true;
    // Existing FAT implementation only. No format-on-failure fallback.
    if (f_mount(&filesystem, volume, 1) != FR_OK) result = ESP_FAIL;
  }
  if (result == ESP_OK && !checkpoint()) result = progress.error;
  mounted = result == ESP_OK;
  endOperation();
  if (result != ESP_OK) unmount();
  return result;
}
}
