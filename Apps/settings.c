#include "T5AppApi.h"

static bool has_settings_api(const t5_app_api_v1 *api) {
    const size_t required = offsetof(t5_app_api_v1, settings_touch) + sizeof(api->settings_touch);
    return api && api->struct_size >= required && api->set_back_exits_app && api->settings_category_count &&
           api->settings_category_get && api->settings_count && api->settings_get && api->settings_activate &&
           api->settings_render && api->settings_touch;
}

static void incompatible_screen(const t5_app_api_v1 *api) {
    if (!api) return;
    api->clear();
    api->draw_text(24, 32, "Settings");
    api->draw_text(24, 84, "This settings app requires newer firmware.");
    api->draw_text(24, 116, "Update firmware, then run settings.elf again.");
    api->present(true);

    t5_app_input_t input;
    while (api->poll(&input, 20)) {
        if (input.exit_requested || (input.buttons & T5_APP_BUTTON_CONFIRM)) return;
    }
}

static void next_category(const t5_app_api_v1 *api, uint32_t *category, int32_t *selected, bool forward) {
    const uint32_t count = api->settings_category_count();
    if (!category || !selected || count == 0) return;

    if (forward) {
        ++(*category);
        if (*category >= count) *category = 0;
    } else {
        if (*category == 0) *category = count - 1;
        else --(*category);
    }

    if (*selected > 0) {
        *selected = api->settings_count(*category) > 0 ? 1 : 0;
    }
}

static bool activate_selected(const t5_app_api_v1 *api, uint32_t category, int32_t selected) {
    if (selected <= 0) return false;
    const uint8_t result = api->settings_activate(category, (uint32_t)(selected - 1));
    return result == T5_APP_SETTING_ACTION_REQUESTED;
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!has_settings_api(api)) {
        incompatible_screen(api);
        return;
    }

    // Settings needs Back for internal navigation. Power and touch Home remain
    // unconditional native-app exit gestures in the host.
    api->set_back_exits_app(false);

    uint32_t category = 0;
    int32_t selected = 0;  // 0 = category tab bar; 1..N = setting rows.
    api->settings_render(category, selected);

    bool armed = false;
    t5_app_input_t input;
    while (api->poll(&input, 20)) {
        if (input.exit_requested) return;

        const uint32_t buttons = input.buttons;
        if (buttons == 0 && !input.tapped) {
            armed = true;
            continue;
        }
        if (!armed) continue;
        armed = false;

        if (input.tapped) {
            const uint8_t result = api->settings_touch(input.touch_x, input.touch_y, &category, &selected);
            if (result == T5_APP_SETTING_ACTION_REQUESTED) return;
            api->settings_render(category, selected);
            continue;
        }

        if (buttons & T5_APP_BUTTON_BACK) {
            if (selected > 0) {
                selected = 0;
                api->settings_render(category, selected);
                continue;
            }
            return;
        }

        if (buttons & T5_APP_BUTTON_CONFIRM) {
            if (selected == 0) {
                next_category(api, &category, &selected, true);
            } else if (activate_selected(api, category, selected)) {
                return;
            }
            api->settings_render(category, selected);
            continue;
        }

        if (buttons & T5_APP_BUTTON_UP) {
            const uint32_t count = api->settings_count(category);
            if (selected <= 0) selected = (int32_t)count;
            else --selected;
            api->settings_render(category, selected);
            continue;
        }

        if (buttons & T5_APP_BUTTON_DOWN) {
            const uint32_t count = api->settings_count(category);
            ++selected;
            if ((uint32_t)selected > count) selected = 0;
            api->settings_render(category, selected);
            continue;
        }

        if (buttons & T5_APP_BUTTON_LEFT) {
            next_category(api, &category, &selected, false);
            api->settings_render(category, selected);
            continue;
        }

        if (buttons & T5_APP_BUTTON_RIGHT) {
            next_category(api, &category, &selected, true);
            api->settings_render(category, selected);
            continue;
        }
    }
}
