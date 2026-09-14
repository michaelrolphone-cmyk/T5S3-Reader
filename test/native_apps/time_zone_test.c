#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5TimeZoneApi.h"
#include "T5UiApi.h"

void app_main(void);

static int polls;
static int renders;
static int selected_region = -1;
static int selected_city = -1;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    if (polls == 1) input->buttons = T5_APP_BUTTON_CONFIRM;
    else if (polls == 2) input->buttons = T5_APP_BUTTON_DOWN;
    else if (polls == 3) input->buttons = T5_APP_BUTTON_CONFIRM;
    else input->buttons = T5_APP_BUTTON_BACK;
    return true;
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = app_poll,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

static uint32_t region_count(void) { return 2; }
static bool region_info(uint32_t region, t5_time_zone_region_info_t *out) {
    assert(out && region < 2);
    memset(out, 0, sizeof(*out));
    strcpy(out->name, region == 0 ? "UTC" : "America");
    out->selected = region == 0;
    return true;
}
static uint32_t city_count(uint32_t region) { assert(region < 2); return region == 0 ? 1 : 2; }
static bool city_info(uint32_t region, uint32_t city, t5_time_zone_city_info_t *out) {
    assert(out);
    memset(out, 0, sizeof(*out));
    if (region == 0) {
        assert(city == 0);
        strcpy(out->id, "UTC");
        strcpy(out->name, "UTC");
        out->selected = 1;
        return true;
    }
    assert(city < 2);
    strcpy(out->id, city == 0 ? "America/Denver" : "America/Chicago");
    strcpy(out->name, city == 0 ? "Denver" : "Chicago");
    return true;
}
static bool select_city(uint32_t region, uint32_t city) {
    selected_region = (int)region;
    selected_city = (int)city;
    return true;
}
static const t5_time_zone_api_v1 zone_api = {
    .api_version = T5_TIME_ZONE_API_VERSION,
    .struct_size = sizeof(t5_time_zone_api_v1),
    .region_count = region_count,
    .region_info = region_info,
    .city_count = city_count,
    .city_info = city_info,
    .select_city = select_city,
};
const t5_time_zone_api_v1 *t5_time_zone_get_api(uint32_t version) {
    return version == T5_TIME_ZONE_API_VERSION ? &zone_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count > 0);
    assert(selected_index >= 0 && (uint32_t)selected_index < row_count);
    ++renders;
}
static int32_t hit_test(int16_t x, int16_t y) { (void)x; (void)y; return T5_UI_HIT_NONE; }
static int32_t next_index(int32_t current, uint32_t count) { return (current + 1) % (int32_t)count; }
static int32_t previous_index(int32_t current, uint32_t count) { return current <= 0 ? (int32_t)count - 1 : current - 1; }
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
    .next_index = next_index,
    .previous_index = previous_index,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    app_main();
    assert(selected_region == 0);
    assert(selected_city == 0);
    assert(polls == 3);
    assert(renders >= 3);
    return 0;
}
