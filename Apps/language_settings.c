#include "T5LanguageApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_LANGUAGES 64u

static const t5_language_api_v1 *language_api;
static const t5_ui_api_v1 *ui;
static t5_language_info_t languages[MAX_LANGUAGES];
static t5_ui_list_row_t rows[MAX_LANGUAGES];
static uint32_t language_count;
static int32_t selected_index;

static void render(void) {
    for (uint32_t i = 0; i < language_count; ++i) {
        rows[i].title = languages[i].name;
        rows[i].subtitle = NULL;
        rows[i].value = languages[i].selected ? "Selected" : "";
        rows[i].flags = languages[i].selected ? T5_UI_LIST_HIGHLIGHT_VALUE : 0;
    }

    const t5_ui_chrome_t chrome = {
        .title = "Language",
        .subtitle = "Select the firmware interface language",
        .status = NULL,
        .back_label = "Back",
        .confirm_label = "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, language_count, selected_index);
}

static bool load_languages(void) {
    language_count = language_api->count();
    if (language_count == 0) return false;
    if (language_count > MAX_LANGUAGES) language_count = MAX_LANGUAGES;

    selected_index = 0;
    for (uint32_t i = 0; i < language_count; ++i) {
        if (!language_api->read(i, &languages[i])) return false;
        if (languages[i].selected) selected_index = (int32_t)i;
    }
    return true;
}

static bool select_current(void) {
    if (selected_index < 0 || (uint32_t)selected_index >= language_count) return false;
    return language_api->select(languages[selected_index].language_id);
}

void app_main(void) {
    language_api = t5_language_get_api(T5_LANGUAGE_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!language_api || !ui || !language_api->count || !language_api->read || !language_api->select ||
        !ui->render_list || !ui->poll_event || !ui->hit_test || !ui->next_index || !ui->previous_index) {
        return;
    }
    if (!load_languages()) return;

    render();
    for (;;) {
        t5_ui_event_t event;
        if (!ui->poll_event(&event, 50)) continue;
        switch (event.type) {
            case T5_UI_EVENT_BACK:
            case T5_UI_EVENT_EXIT:
                return;
            case T5_UI_EVENT_CONFIRM:
                if (select_current()) return;
                break;
            case T5_UI_EVENT_NEXT:
                selected_index = ui->next_index(selected_index, language_count);
                render();
                break;
            case T5_UI_EVENT_PREVIOUS:
                selected_index = ui->previous_index(selected_index, language_count);
                render();
                break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit >= 0 && (uint32_t)hit < language_count) {
                    selected_index = hit;
                    if (select_current()) return;
                }
                break;
            }
            default:
                break;
        }
    }
}
