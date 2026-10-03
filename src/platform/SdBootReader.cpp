#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
#include "SdBootReader.h"
#include <Arduino.h>
#include <Logging.h>
#include "driver/gpio.h"
#if defined(BOARD_T5S3_PRO)
#include <BoardT5S3.h>
#include <SPI.h>
#include <SdSpiFault.h>
#include <SdCard/SdSpiCard/SdSpiCard.h>
#else
#include "x4pro_pins.h"
#endif
#include <algorithm>
#include <atomic>
#include <cstring>
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "SdBootFatFs.h"

namespace SdBootReader {
struct Progress { uint32_t sectors; uint32_t commands; esp_err_t error; };
bool beginOperation(uint32_t milliseconds, uint32_t maxSectors);
Progress endOperation();
esp_err_t unmount();
namespace {
constexpr uint32_t kSectorBytes = 512;
constexpr uint32_t kMaxSectors = 8192;
constexpr uint32_t kMaxCommands = 16384;
constexpr int kCommandTimeoutMs = 250;
std::atomic<bool> claimed{false}, cancelled{false};
TaskHandle_t owner = nullptr;
#if defined(BOARD_T5S3_PRO)
SdSpiCard spiCard;
#else
sdmmc_card_t card{};
#endif
uint32_t sectors=0;
FATFS filesystem{};
BYTE disk = 0;
char volume[4]="0:";
bool hostUp = false, diskRegistered = false, mounted = false;
bool closeFailed = false;
bool operation = false;
int64_t deadline = 0;
uint32_t remainingSectors = 0, traversalSteps=0;
int64_t lastYield=0;
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
#if defined(BOARD_XTEINK_X4_PRO)
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

#endif
DSTATUS status(BYTE which) {
  if (!onOwner() || which != disk || !hostUp) return STA_NOINIT;
  return STA_PROTECT;
}
DRESULT transfer(BYTE which, BYTE* dst, const BYTE* src, DWORD sector, UINT count) {
  if (which != disk || !checkpoint() || !count || count > remainingSectors ||
      sector >= sectors ||
      count > sectors - sector) {
    progress.error = ESP_ERR_INVALID_SIZE;
    return RES_PARERR;
  }
  if (src) return RES_WRPRT;
  if ((!src && !dst) || count > kMaxSectors) return RES_PARERR;
  for (UINT i = 0; i < count; ++i) {
    if (!checkpoint()) return RES_ERROR;
    --remainingSectors;
    esp_err_t result;
#if defined(BOARD_T5S3_PRO)
    result = spiCard.readSector(sector+i,bounce) ? ESP_OK : ESP_FAIL;
#else
    result = sdmmc_read_sectors(&card, bounce, sector + i, 1);
#endif
    if (result == ESP_OK) std::memcpy(dst + i * kSectorBytes, bounce, kSectorBytes);
    ++progress.sectors;
    if (result != ESP_OK) { progress.error = result; return RES_ERROR; }
    vTaskDelay(1);
  }
  return checkpoint() ? RES_OK : RES_ERROR;
}
DRESULT readDisk(BYTE d, BYTE* p, DWORD s, UINT n) { return transfer(d, p, nullptr, s, n); }
DRESULT control(BYTE which, BYTE command, void* value) {
  if (which != disk || !checkpoint()) return RES_NOTRDY;
  switch (command) {
    case CTRL_SYNC: return RES_OK; // Each sector operation is synchronous.
    case GET_SECTOR_COUNT:
      if (!value) return RES_PARERR;
      *static_cast<DWORD*>(value) = sectors; return RES_OK;
    case GET_SECTOR_SIZE:
      if (!value) return RES_PARERR;
      *static_cast<WORD*>(value) = kSectorBytes; return RES_OK;
    case GET_BLOCK_SIZE:
      if (!value) return RES_PARERR;
      *static_cast<DWORD*>(value) = 1; return RES_OK;
    default: return RES_PARERR; // No trim/erase/format operation exposed.
  }
}

}

bool beginOperation(uint32_t milliseconds, uint32_t maxSectors) {
  if (!onOwner() || operation || !milliseconds || milliseconds > 20000 ||
      !maxSectors || maxSectors > kMaxSectors) return false;
  cancelled.store(false);
  deadline = esp_timer_get_time() + static_cast<int64_t>(milliseconds) * 1000;
  remainingSectors = maxSectors;
  traversalSteps=0;lastYield=esp_timer_get_time();
  progress = {};
#if defined(BOARD_T5S3_PRO)
  risc_sd_spi_begin_operation();
#endif
  operation = true;
  return true;
}
Progress endOperation() {
  if (!onOwner()) return Progress{0, 0, ESP_ERR_INVALID_STATE};
  operation = false;
#if defined(BOARD_T5S3_PRO)
  risc_sd_spi_end_operation();
#endif
  return progress;
}
const char* drive() { return onOwner() && mounted ? volume : nullptr; }
void cancel() { cancelled.store(true); }
bool retained() { return claimed.load(); }

esp_err_t unmount() {
  if (!onOwner() || operation || closeFailed) return ESP_ERR_INVALID_STATE;
  // Caller closes all handles first. Never run a second physical owner when
  // deinitialization is uncertain: retain claimed state on a deinit failure.
  if (diskRegistered) {
    if (f_mount(nullptr, volume, 0) != FR_OK) return ESP_FAIL;
    diskRegistered = mounted = false;
  }
  if (hostUp) {
#if defined(BOARD_T5S3_PRO)
    // Single-sector reads leave no asynchronous request. syncDevice is checked
    // before end(); the patched FSPI port retains the task on uncertain stalls.
    SdSpiOperation cleanup;
    if(risc_sd_spi_faulted() || !spiCard.syncDevice()) return ESP_FAIL;
    spiCard.end();
    SPI.end();
    const esp_err_t result=(!SPI.bus() && !risc_sd_spi_faulted()) ? ESP_OK : ESP_FAIL;
#else
    const esp_err_t result = sdmmc_host_deinit();
#endif
    if (result != ESP_OK) return result;
    hostUp = false;
  }
  operation = false;
  owner = nullptr;
  claimed.store(false);
  return ESP_OK;
}
esp_err_t mountDisk() {
  bool expected = false;
  if (!claimed.compare_exchange_strong(expected, true)) return ESP_ERR_INVALID_STATE;
  owner = xTaskGetCurrentTaskHandle();
  sectors=0;
  if (!beginOperation(20000, 4096)) { unmount(); return ESP_ERR_INVALID_STATE; }
#if defined(BOARD_T5S3_PRO)
  // Minimal card reads only, through the existing guarded FSPI transport. No
  // SdFat filesystem/normal Storage binding and no IDF SPI polling fallback.
  // Board::prepareSdBus and LoRa have not run; refuse a preexisting controller.
  if(SPI.bus() || risc_sd_spi_faulted()) { endOperation(); return ESP_ERR_INVALID_STATE; }
  digitalWrite(T5S3_LORA_CS,HIGH); pinMode(T5S3_LORA_CS,OUTPUT);
  digitalWrite(T5S3_SD_CS,HIGH); pinMode(T5S3_SD_CS,OUTPUT);
  SPI.begin(T5S3_SPI_SCLK,T5S3_SPI_MISO,T5S3_SPI_MOSI,T5S3_SD_CS);
  hostUp=SPI.bus()!=nullptr;
  esp_err_t result=hostUp && spiCard.begin(SdSpiConfig(T5S3_SD_CS,SHARED_SPI,SD_SCK_MHZ(20),&SPI))
      ? ESP_OK : ESP_FAIL;
  if(result==ESP_OK) sectors=spiCard.sectorCount();
  if(!sectors) result=ESP_ERR_NOT_SUPPORTED;
#else
  // X4 card power is active low, matching the extracted SD provider.
  gpio_set_direction(static_cast<gpio_num_t>(X4PRO_PIN_SD_PWR),GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(X4PRO_PIN_SD_PWR),0);
  vTaskDelay(pdMS_TO_TICKS(10));
  card={};
  sdmmc_host_t host=SDMMC_HOST_DEFAULT();
  host.flags=SDMMC_HOST_FLAG_1BIT;
  sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width=1; slot.clk=static_cast<gpio_num_t>(X4PRO_PIN_SD_CLK);
  slot.cmd=static_cast<gpio_num_t>(X4PRO_PIN_SD_CMD);
  slot.d0=static_cast<gpio_num_t>(X4PRO_PIN_SD_DAT0);
  esp_err_t result=sdmmc_host_init();
  if(result==ESP_OK) { hostUp=true; result=sdmmc_host_init_slot(host.slot,&slot); }
  host.max_freq_khz=SDMMC_FREQ_DEFAULT;
  host.command_timeout_ms=kCommandTimeoutMs;
  host.do_transaction=transaction;
  if (result == ESP_OK) result = sdmmc_card_init(&host, &card);
  if (result == ESP_OK && (card.csd.sector_size != kSectorBytes || card.csd.capacity <= 0))
    result = ESP_ERR_NOT_SUPPORTED;
  if(result==ESP_OK) sectors=static_cast<uint32_t>(card.csd.capacity);
#endif
  if (result == ESP_OK) {
    diskRegistered = true;
    // Existing bounded FatFs, read-only/minimal/private build. No format API.
    if (f_mount(&filesystem, volume, 1) != FR_OK) result = ESP_FAIL;
  }
  if (result == ESP_OK && !checkpoint()) result = progress.error;
  mounted = result == ESP_OK;
  endOperation();
  if (result != ESP_OK) (void)unmount();
  return result;
}

bool filesystemCheckpoint() {
  if(!checkpoint() || ++traversalSteps>1048576u) return false;
  const int64_t now=esp_timer_get_time();
  if((traversalSteps&255u)==0 || now-lastYield>=8000) {
    vTaskDelay(1);lastYield=esp_timer_get_time();
  }
  return checkpoint();
}
// No writable API, directory mutation, runtime volume binding or retry mount.
bool mount() { return mountDisk()==ESP_OK; }
bool release() { return unmount()==ESP_OK; }
bool read(const std::string& path, size_t limit, std::vector<uint8_t>& bytes) {
  bytes.clear();
  if(!mounted || !onOwner() || closeFailed || path.size()>240 ||
     path.compare(0,7,"/sdboot") || path.size()<9 || path[7]!='/' ||
     !limit || limit>1024u*1024u || !beginOperation(15000,8192)) return false;
  FIL file{};
  const std::string name=std::string(volume)+path.substr(7);
  bool opened=f_open(&file,name.c_str(),FA_READ)==FR_OK;
  bool good=opened && f_size(&file)>0 && f_size(&file)<=limit;
  if(good) bytes.resize(static_cast<size_t>(f_size(&file)));
  for(size_t at=0;good && at<bytes.size();) {
    if(!checkpoint()) { good=false;break; }
    const UINT amount=static_cast<UINT>(std::min<size_t>(4096,bytes.size()-at));
    UINT got=0;
    good=f_read(&file,bytes.data()+at,amount,&got)==FR_OK && got==amount;
    at+=got;
    vTaskDelay(1);
  }
  if(opened && f_close(&file)!=FR_OK) { closeFailed=true;good=false; }
  const auto completed=endOperation();
  good=good && completed.error==ESP_OK;
  if(!good) bytes.clear();
  return good;
}
}
extern "C" {
int risc_boot_fatfs_checkpoint(void) { return SdBootReader::filesystemCheckpoint(); }
DSTATUS disk_initialize(BYTE d) { return SdBootReader::status(d); }
DSTATUS disk_status(BYTE d) { return SdBootReader::status(d); }
DRESULT disk_read(BYTE d,BYTE* p,LBA_t sector,UINT count) {
  return SdBootReader::readDisk(d,p,sector,count);
}
DRESULT disk_ioctl(BYTE d,BYTE command,void* value) { return SdBootReader::control(d,command,value); }
}
#endif
