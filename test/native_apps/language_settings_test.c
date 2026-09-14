#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5LanguageApi.h"
#include "T5UiApi.h"

void app_main(void);

static int poll_count;
static int render_count;
static int selected_language = -1;
static int32_t last_selected = -1;

static const char *names[] = {"English", "Deutsch", "Español"};
static const uint8_t ids[] = {0, 2, 1};

static uint32_t language_count(void) { return 3; }

static bool language_read(uint32_t index, t5_language_info_t *info) {
    assert(info && index < 3);
    memset(info, 0, sizeof(*info));
    info->language_id = ids[index];
    info->selected = index == 1;
    strncpy(info->name, names[index], sizeof(info->name) - 1);
    return true;
}

static bool language_select(uint8_t language_id) {
    selected_language = language_id;
    return true;
}

static const t5_language_api_v1 language_api = {
    .api_version = T5_LANGUAGE_API_VERSION,
    .struct_size = sizeof(t5_language_api_v1),
    .count = language_count,
    .read = language_read,
    .select = language_select,
};

const t5_language_api_v1 *t5_language_get_api(uint32_t version) {
    return version == T5_LANGUAGE_API_VERSION ? &language_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 3);
    assert(strcmp(chrome->title, "Language") == 0);
    assert(strcmp(rows[0].title, "English") == 0);
    assert(strcmp(rows[1].title, "Deutsch") == 0);
    assert(strcmp(rows[1].value, "Selected") == 0);
    last_selected = selected_index;
    ++render_count;
}

static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    assert(event && wait_ms == 50);
    memset(event, 0, sizeof(*event));
    ++poll_count;
    if (poll_count == 1) event->type = T5_UI_EVENT_NEXT;
    else event->type = T5_UI_EVENT_CONFIRM;
    return true;
}

static int32_t hit_test(int16_t x, int16_t y) {
    (void)x;
    (void)y;
    return T5_UI_HIT_NONE;
}

static int32_t next_index(int32_t current, uint32_t count) {
    return (current + 1) % (int32_t)count;
}

static int32_t previous_index(int32_t current, uint32_t count) {
    return current <= 0 ? (int32_t)count - 1 : current - 1;
}

static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
    .poll_event = poll_event,
    .next_index = next_index,
    .previous_index = previous_index,
};

const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    app_main();
    assert(render_count == 2);
    assert(last_selected == 2);
    assert(selected_language == 1);
    return 0;
}
