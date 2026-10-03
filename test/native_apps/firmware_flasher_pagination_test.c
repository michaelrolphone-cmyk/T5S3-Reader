#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../Apps/esp_rom_flasher.c"

static uint32_t file_total;
static uint32_t dir_index;
static uint32_t fake_clock;
static unsigned dir_closes;
static unsigned yield_calls;
static bool fail_open;
static bool cancel_scan;

static bool mock_dir_open(const char *path) {
    assert(strcmp(path, "/sd") == 0);
    dir_index = 0;
    return !fail_open;
}
static bool mock_dir_next(t5_app_dirent_t *entry) {
    if (dir_index >= file_total) return false;
    memset(entry, 0, sizeof(*entry));
    (void)snprintf(entry->name, sizeof(entry->name), "firmware-%03lu.bin",
                   (unsigned long)dir_index);
    entry->size = 1024u + dir_index;
    ++dir_index;
    return true;
}
static void mock_dir_close(void) { ++dir_closes; }
static uint32_t mock_millis(void) { return fake_clock++; }
static bool mock_poll(t5_app_input_t *input, uint32_t wait) {
    assert(wait == 1u);
    ++yield_calls;
    memset(input, 0, sizeof(*input));
    input->exit_requested = cancel_scan;
    return true;
}
static const t5_app_api_v1 mock_app = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = mock_poll,
    .millis = mock_millis,
    .dir_open = mock_dir_open,
    .dir_next = mock_dir_next,
    .dir_close = mock_dir_close,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    (void)version; return &mock_app;
}
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    (void)version; return NULL;
}
const t5_stream_api_v1 *t5_stream_get_api(uint32_t version) {
    (void)version; return NULL;
}
const t5_program_esp_rom_api_v1 *t5_program_esp_rom_get_api(uint32_t version) {
    (void)version; return NULL;
}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t version) {
    (void)version; return NULL;
}

static void set_inventory(uint32_t count) {
    file_total = count;
    dir_index = fake_clock = 0;
    dir_closes = yield_calls = 0;
    fail_open = cancel_scan = false;
}

int main(void) {
    app = &mock_app;

    set_inventory(64);
    assert(scan_page(0));
    assert(image_count == 64 && !has_next_page && !has_previous_page);
    assert(strcmp(images[63], "firmware-063.bin") == 0);
    selected = 63;
    move_next_image();
    assert(image_offset == 0 && selected == 63);

    set_inventory(65);
    assert(scan_page(0));
    assert(image_count == 64 && has_next_page);
    selected = 63;
    move_next_image();
    assert(image_offset == 64 && image_count == 1 && !has_next_page);
    assert(has_previous_page && selected == 0);
    assert(strcmp(images[0], "firmware-064.bin") == 0);
    move_previous_image();
    assert(image_offset == 0 && image_count == 64 && selected == 63);

    set_inventory(130);
    assert(scan_page(0));
    selected = 63;
    move_next_image();
    assert(image_offset == 64 && image_count == 64 && has_next_page);
    selected = 63;
    move_next_image();
    assert(image_offset == 128 && image_count == 2 && !has_next_page);
    assert(strcmp(images[0], "firmware-128.bin") == 0);
    assert(strcmp(images[1], "firmware-129.bin") == 0);
    move_previous_image();
    assert(image_offset == 64 && image_count == 64 && selected == 63);

    set_inventory(65);
    fail_open = true;
    assert(!scan_page(0));
    assert(scan_failed && strstr(failure_text, "retry") != NULL);
    fail_open = false;
    move_next_image();
    assert(!scan_failed && image_count == 64 && has_next_page);

    set_inventory(130);
    cancel_scan = true;
    assert(!scan_page(0));
    assert(scan_failed && strstr(failure_text, "cancelled") != NULL);
    cancel_scan = false;
    move_next_image();
    assert(!scan_failed && image_count == 64 && has_next_page);
    assert(yield_calls > 0 && dir_closes > 0);
    return 0;
}
