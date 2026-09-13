#include "T5AppApi.h"

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!api || api->struct_size < sizeof(*api)) return;
    api->clear();
    api->draw_text(30, 60, "Hello from a native ELF in PSRAM");
    api->draw_text(30, 110, "Tap to draw. Back / PWR / Home exits.");
    api->present(true);
    t5_app_input_t input;
    while (api->poll(&input, 20)) {
        if (input.exit_requested) return;
        if (input.tapped) {
            api->fill_rect(input.touch_x, input.touch_y, 12, 12, true);
            api->present(false);
        }
    }
}
