#include "RiscCpuCacheV2.h"
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "esp32s3/rom/cache.h"
#include "soc/soc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* Keep the SDK's CACHE-126 workaround; never replace the ROM wrapper with a
 * bare register loop or widen the existing executable-publication operation. */
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4,4,6) || \
 (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0) && ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,4)) || \
 (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,1,0) && ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,1,1))
#error "CPU cache ABI requires the ESP32-S3 CACHE-126 fix"
#endif
static DRAM_ATTR portMUX_TYPE cache_lock=portMUX_INITIALIZER_UNLOCKED;
int IRAM_ATTR risc_cpu_cache_writeback_v2(uintptr_t address,size_t bytes) {
    if(xPortInIsrContext())return -2;
    /* Exclusive end: subtract before adding, preventing uintptr_t overflow.
     * The EXTRAM window excludes flash and its executable I-bus alias. */
    if(!bytes || bytes>1024u*1024u || address<SOC_EXTRAM_DATA_LOW ||
       address>=SOC_EXTRAM_DATA_HIGH || bytes>SOC_EXTRAM_DATA_HIGH-address)return -1;
    const int64_t began=esp_timer_get_time();
    int64_t checkpoint=began;
    size_t offset=0,since_yield=0;
    unsigned lock_polls=0;
    while(offset<bytes) {
        int64_t now=esp_timer_get_time();
        if(now-began>=100000 || lock_polls>=100)return -5;
        // Never spin forever with interrupts masked waiting on another core.
        if(!portTRY_ENTER_CRITICAL(&cache_lock,0)) {
            ++lock_polls;vTaskDelay(1);continue;
        }
        size_t chunk=bytes-offset;if(chunk>4096)chunk=4096;
        int rc=Cache_WriteBack_Addr(address+offset,chunk);
        portEXIT_CRITICAL(&cache_lock);
        if(rc)return -4;
        offset+=chunk;since_yield+=chunk;
        now=esp_timer_get_time();
        if(now-began>=100000)return -5;
        if(offset<bytes && (since_yield>=32768 || now-checkpoint>=2000)) {
            vTaskDelay(1);checkpoint=esp_timer_get_time();since_yield=0;
        }
    }
    return 0;
}
#else
int risc_cpu_cache_writeback_v2(uintptr_t address,size_t bytes) {
    (void)address;(void)bytes;return -3;
}
#endif
