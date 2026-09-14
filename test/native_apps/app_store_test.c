#include "T5AppApi.h"

#include <assert.h>
#include <string.h>

void app_main(void);

static int ticks;
static int icons;
static int downloads;
static int saw_alpha;
static int saw_beta;
static uint32_t downloaded_index;

static int32_t width(void) { return 540; }
static int32_t height(void) { return 960; }
static void clear(void) {}
static void text(int32_t x, int32_t y, const char *s) {
    (void)x;
    (void)y;
    if (!strcmp(s, "Alpha")) saw_alpha = 1;
    if (!strcmp(s, "Beta")) saw_beta = 1;
}
static void rect(int32_t x, int32_t y, int32_t w, int32_t h, bool black) {
    (void)black;
    assert(x >= 0 && y >= 0 && w >= 0 && h >= 0);
    assert(x + w <= 540 && y + h <= 960);
}
static void present(bool full) { (void)full; }
static bool refresh(void) { return true; }
static uint32_t count(void) { return 2; }
static bool asset_get(uint32_t index, t5_app_release_asset_t *asset) {
    assert(index < count());
    memset(asset, 0, sizeof(*asset));
    strcpy(asset->name, index == 0 ? "alpha.elf" : "beta.elf");
    asset->size = 1234 + index;
    return true;
}
static bool manifest_get(uint32_t index, t5_app_manifest_t *manifest) {
    assert(index < count());
    memset(manifest, 0, sizeof(*manifest));
    strcpy(manifest->display_name, index == 0 ? "Alpha" : "Beta");
    strcpy(manifest->file_name, index == 0 ? "alpha.elf" : "beta.elf");
    strcpy(manifest->min_firmware_version, "1.1.5");
    strcpy(manifest->icon, index == 0 ? "solid:f013" : "regular:f007");
    manifest->compatible = true;
    return true;
}
static bool download(uint32_t index) {
    assert(index < count());
    ++downloads;
    downloaded_index = index;
    return true;
}
static bool icon(int32_t x, int32_t y, const char *name, uint8_t point_size, bool black) {
    (void)x;
    (void)y;
    (void)black;
    assert(name && name[0]);
    assert(point_size == 18);
    ++icons;
    return true;
}
static bool poll(t5_app_input_t *input, uint32_t wait_ms) {
    (void)wait_ms;
    memset(input, 0, sizeof(*input));
    ++ticks;
    if (ticks == 2) input->buttons = T5_APP_BUTTON_DOWN;
    if (ticks == 4) input->buttons = T5_APP_BUTTON_CONFIRM;
    if (ticks == 6) input->buttons = T5_APP_BUTTON_CONFIRM;
    if (ticks >= 7) input->exit_requested = true;
    return true;
}

static const t5_app_api_v1 api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .screen_width = width,
    .screen_height = height,
    .clear = clear,
    .draw_text = text,
    .fill_rect = rect,
    .present = present,
    .poll = poll,
    .app_catalog_refresh = refresh,
    .app_catalog_count = count,
    .app_catalog_get = asset_get,
    .app_catalog_download = download,
    .draw_icon = icon,
    .app_catalog_manifest_get = manifest_get,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    assert(version == T5_APP_ABI_VERSION);
    return &api;
}

int main(void) {
    app_main();
    assert(saw_alpha && saw_beta);
    assert(icons >= 2);
    assert(downloads == 1);
    assert(downloaded_index == 1);
    return 0;
}
