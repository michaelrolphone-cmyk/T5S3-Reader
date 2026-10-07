#include <T5VideoApi.h>
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <driver/gpio.h>
#include "RiscDisplayPowerV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscCpuWorkerV2.h"
#include "RiscCpuCacheV2.h"
#include "T5DisplayProviderV1.h"

// Set by the provider's dependency admission; never firmware bridge callbacks.
extern const risc_display_power_api_v1 *display_power;
extern const risc_platform_clock_api_v1 *display_clock;
static inline uint32_t millis() { return static_cast<uint32_t>(display_clock->monotonic_ms(display_clock->context)); }
static inline void delayMicroseconds(uint32_t us) { esp_rom_delay_us(us); }
extern "C" const t5_video_api_v1 *display_fast_api(uint32_t version);
extern "C" const t5_display_quality_api_v1 *display_quality_api(void);
bool display_fast_force_stop();
