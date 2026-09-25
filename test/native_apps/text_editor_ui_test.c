#include "../../Apps/text_editor.c"
#include <assert.h>
static int32_t test_width = 540, test_height = 960;
static unsigned polls, acquisitions, labels;
static bool inside, keyboard_ok, refreshing;
static unsigned generated, received;
static const risc_usb_keyboard_api_v1 fake_keyboard;
static char rendered_error[160];
static const char failure[] = "usb-controller-esp32s3: usb-host-install rc=259 (0x103)";
static int32_t width(void) { return test_width; }
static int32_t height(void) { return test_height; }
static void clear(void) { rendered_error[0] = 0; }
static void present(bool full) { (void)full; }
static void back(bool enabled) { (void)enabled; }
static uint32_t millis(void) { return 200; }
static void text(int32_t x, int32_t y, const char *value) {
    (void)value; assert(x >= 0 && y >= 0 && y + 30 <= test_height);
}
static void label(int32_t x, int32_t y, int32_t w, const char *value) {
    assert(x >= 0 && x + w <= test_width); text(x, y, value); ++labels;
    assert(!strstr(value, "Confirm"));
    if (keyboard_error[0] && y >= 88 && y < 248) {
        assert(strlen(rendered_error) + strlen(value) < sizeof(rendered_error));
        strcat(rendered_error, value);
    }
}
static void rect(int32_t x, int32_t y, int32_t w, int32_t h, bool black) {
    (void)black; assert(x >= 0 && y >= 0 && x + w <= test_width && y + h <= test_height);
}
static bool poll_input(t5_app_input_t *input, uint32_t wait) {
    (void)wait;
    if (polls++ == 2) return false;
    input->tapped = true;
    input->touch_x = inside ? test_width / 2 : 0;
    input->touch_y = test_height - 30;
    return true;
}
static bool acquire(const char *name, uint32_t version, t5_provider_capability_lease_t *lease, const void **api) {
    (void)version; (void)lease; (void)api;
    assert(!strcmp(name, "usb.hid.keyboard")); ++acquisitions;
    if (keyboard_ok) { *lease = 1; *api = &fake_keyboard; return true; }
    return false;
}
static bool release(t5_provider_capability_lease_t lease) { (void)lease; return true; }
static bool exists(const char *p) { (void)p; return false; }
static bool read_file(const char *p, void *b, size_t c, size_t *n) { (void)p;(void)b;(void)c;(void)n;return false; }
static bool write_file(const char *p, const void *b, size_t n) { (void)p;(void)b;(void)n;return false; }
static bool dir_open(const char *p) { (void)p;return false; }
static bool dir_next(t5_app_dirent_t *e) { (void)e;return false; }
static void dir_close(void) {}
static uint64_t key_subscribe(void *ctx, uint64_t device) { (void)ctx; (void)device; return 1; }
static bool key_unsubscribe(void *ctx, uint64_t token) { (void)ctx; (void)token; return true; }
static bool key_poll(void *ctx, size_t budget) {
    (void)ctx; assert(budget == 4);
    if (refreshing && generated < 40) generated += 4;
    return true;
}
static int32_t key_next(void *ctx, uint64_t token, risc_usb_keyboard_event_v1 *event) {
    (void)ctx; (void)token;
    if (received == generated) return 0;
    *event = (risc_usb_keyboard_event_v1){0};
    event->kind = received % 2 ? 4 : 3;
    event->usage = 4 + received / 2;
    ++received;
    return 1;
}
static bool key_snapshot(void *ctx, risc_usb_keyboard_state_v1 *states, size_t *count) {
    (void)ctx; (void)states; *count = 0; return true;
}
static const risc_usb_keyboard_api_v1 fake_keyboard = {
    1, sizeof(risc_usb_keyboard_api_v1), NULL, key_subscribe, key_unsubscribe,
    key_poll, key_next, key_snapshot
};
static bool serviced_present(bool full, void (*service)(void *), void *context) {
    (void)full;
    refreshing = true;
    for (unsigned i = 0; i < 10; ++i) service(context);
    refreshing = false;
    assert(document.length == 0); // Input collected; no document/UI action during refresh.
    return true;
}
static const t5_app_api_v1 fake_app = {
    .abi_version=T5_APP_ABI_VERSION, .struct_size=sizeof(t5_app_api_v1),
    .screen_width=width, .screen_height=height, .clear=clear, .present=present,
    .draw_text=text, .draw_label=label, .fill_rect=rect, .poll=poll_input,
    .millis=millis, .set_back_exits_app=back, .dir_open=dir_open,
    .dir_next=dir_next, .dir_close=dir_close, .present_serviced=serviced_present
};
static const t5_storage_api_v1 fake_storage = {
    .api_version=T5_STORAGE_API_VERSION, .struct_size=sizeof(t5_storage_api_v1),
    .exists=exists, .read_file=read_file, .write_file_atomic=write_file
};
static bool last_error(char *out, size_t size) {
    (void)snprintf(out, size, "%s", failure); return true;
}
static t5_provider_capability_api_v1 fake_providers = {
    T5_PROVIDER_CAPABILITY_API_VERSION, sizeof(t5_provider_capability_api_v1), acquire, release, last_error
};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { (void)v;return &fake_app; }
const t5_storage_api_v1 *t5_storage_get_api(uint32_t v) { (void)v;return &fake_storage; }
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v) { (void)v;return &fake_providers; }
int main(void) {
    for (unsigned landscape = 0; landscape < 2; ++landscape) {
        test_width = landscape ? 960 : 540; test_height = landscape ? 540 : 960;
        inside = true; polls = acquisitions = labels = 0; app_main();
        assert(!strcmp(keyboard_error, failure));
        assert(!strcmp(rendered_error, failure));
        assert(acquisitions == 1 && labels >= 3); // One automatic probe; no touch activation or retry elapsed.
        inside = false; polls = acquisitions = 0; app_main(); assert(acquisitions == 1);
    }
    fake_providers.struct_size = offsetof(t5_provider_capability_api_v1, last_error);
    inside = true; polls = 0; app_main();
    assert(strstr(keyboard_error, "Update firmware"));
    fake_providers.struct_size = sizeof(fake_providers);
    keyboard_ok = true; inside = true; polls = acquisitions = 0;
    generated = received = 0;
    app_main();
    assert(generated == 40 && received == 40);
    assert(document.length == 20);
    assert(!memcmp(document.text, "abcdefghijklmnopqrst", 20));
    puts("text_editor_ui_test: PASS (ordered typing during refresh included)");
}
