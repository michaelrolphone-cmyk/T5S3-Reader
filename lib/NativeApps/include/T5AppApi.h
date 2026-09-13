#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define T5_APP_ABI_VERSION 1u
#define T5_APP_BUTTON_BACK (1u << 0)
#define T5_APP_BUTTON_CONFIRM (1u << 1)
#define T5_APP_BUTTON_LEFT (1u << 2)
#define T5_APP_BUTTON_RIGHT (1u << 3)
#define T5_APP_BUTTON_UP (1u << 4)
#define T5_APP_BUTTON_DOWN (1u << 5)

typedef struct {
    uint32_t buttons;
    bool tapped;
    int16_t touch_x;
    int16_t touch_y;
    bool exit_requested; // Sticky after Back, PWR or touch Home.
} t5_app_input_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*screen_width)(void);
    int32_t (*screen_height)(void);
    void (*clear)(void);
    void (*draw_text)(int32_t x, int32_t y, const char *text);
    void (*fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, bool black);
    void (*present)(bool full_refresh);
    // Poll at least every 20-50ms. Updates input, yields, and feeds the watchdog.
    // Returns false outside the application's owning task.
    bool (*poll)(t5_app_input_t *input, uint32_t wait_ms);
    uint32_t (*millis)(void);
} t5_app_api_v1;

// This is the single versioned firmware symbol imported by native UI apps.
// NULL means unsupported ABI or no active UI session. Do not call from workers.
const t5_app_api_v1 *t5_app_get_api(uint32_t abi_version);
#ifdef __cplusplus
}
#endif
