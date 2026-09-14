#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_SYSTEM_UI_API_VERSION 1u

typedef enum {
    T5_SYSTEM_KEYBOARD_TEXT = 0,
    T5_SYSTEM_KEYBOARD_PASSWORD = 1,
    T5_SYSTEM_KEYBOARD_URL = 2,
} t5_system_keyboard_type_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Opens the firmware's standard KeyboardEntryActivity after the current ELF
    // returns. The host automatically relaunches the same ELF when the keyboard
    // closes. `cookie` is opaque caller state returned unchanged with the result.
    // max_length == 0 keeps the firmware keyboard's unlimited-length behavior.
    bool (*keyboard_request)(const char *title, const char *initial_text,
                             size_t max_length, uint8_t input_type, uint64_t cookie);

    // Called by the relaunched ELF. Returns false when no keyboard result is
    // pending for consumption. `text` may be NULL when only cancel/cookie state
    // is needed. The result is consumed only after a successful call.
    bool (*keyboard_take_result)(char *text, size_t capacity, bool *cancelled, uint64_t *cookie);
} t5_system_ui_api_v1;

// Versioned firmware system-UI service. This is intentionally separate from
// t5_app_api_v1 so reusable system widgets can grow without destabilizing the
// core native-app ABI.
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
