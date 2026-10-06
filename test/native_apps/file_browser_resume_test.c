/* Full production app; service boundaries model storage, results and rendering. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef FILE_BROWSER_SOURCE
#define FILE_BROWSER_SOURCE "../../Apps/file_browser.c"
#endif
#include FILE_BROWSER_SOURCE

static char session[1400];
static size_t session_bytes;
static bool session_present, session_read_fails, session_remove_fails;
static bool have_open, have_launch, have_rename, have_delete, cancel_rename, confirm_delete;
static bool mutation_fails, destination_exists, usb_enabled, usb_ready = true;
static bool mutation_refresh_fails;
static unsigned refresh_calls, refresh_failures;
static int result_error, open_failures, stop_after = -1;
static const char *failure_path;
static unsigned opens, closes, nexts, render_calls, polls, back_on, back_off, removes;
static unsigned sd_opens, usb_opens, mutations, acquisitions, releases, home_calls;
static bool directory_open, lease_live, launch_requested, use_back, poll_fails;
static char directory[PATH_CAP], shown_path[PATH_CAP], shown_status[STATUS_CAP];
static char names[8][T5_APP_DIRENT_NAME_MAX];
static unsigned name_count, cursor, shown_count;
static int32_t shown_selection;
static bool start_handler;
static char renamed_to[T5_APP_DIRENT_NAME_MAX] = "renamed.bmp";

static bool open_directory(const char *path, bool usb) {
    assert(!directory_open);
    ++opens;
    if (usb) ++usb_opens; else ++sd_opens;
    snprintf(directory, sizeof(directory), "%s", path);
    if (open_failures > 0 && (!failure_path || !strcmp(path, failure_path))) { --open_failures; return false; }
    directory_open = true;
    cursor = 0;
    return true;
}
static bool next_directory(char *name, size_t cap, bool *is_dir) {
    assert(directory_open);
    ++nexts;
    if (cursor == name_count || (stop_after >= 0 && cursor == (unsigned)stop_after)) return false;
    snprintf(name, cap, "%s", names[cursor++]);
    *is_dir = false;
    return true;
}
static void close_directory(void) { assert(directory_open); directory_open = false; ++closes; }
static bool app_dir_open(const char *path) { return open_directory(path, false); }
static bool app_dir_next(t5_app_dirent_t *out) {
    bool is_dir;
    bool okay = next_directory(out->name, sizeof(out->name), &is_dir);
    if (okay) out->is_directory = is_dir;
    return okay;
}
static void back_exits(bool on) { if (on) ++back_on; else ++back_off; }
static uint32_t clock_ms(void) { return 1000; }
static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION, .struct_size = sizeof(t5_app_api_v1),
    .dir_open = app_dir_open, .dir_next = app_dir_next, .dir_close = close_directory,
    .set_back_exits_app = back_exits, .millis = clock_ms,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) { assert(version == T5_APP_ABI_VERSION); return &app_api; }

static bool read_session(const char *path, void *out, size_t cap, size_t *size) {
    assert(!strcmp(path, SESSION_PATH));
    if (!session_present || session_read_fails) return false;
    *size = session_bytes;
    if (session_bytes > cap) return false;
    memcpy(out, session, session_bytes);
    return true;
}
static bool write_session(const char *path, const void *data, size_t size) {
    assert(!strcmp(path, SESSION_PATH) && size < sizeof(session));
    memcpy(session, data, size); session_bytes = size; session_present = true; return true;
}
static bool remove_session(const char *path) {
    assert(!strcmp(path, SESSION_PATH)); ++removes;
    if (session_remove_fails) return false;
    session_present = false; return true;
}
static bool exists(const char *path) { assert(strstr(path, renamed_to)); return destination_exists; }
static bool rename_file(const char *from, const char *to) {
    assert(strstr(from, "second.bmp") && strstr(to, renamed_to)); ++mutations;
    if (mutation_fails) return false;
    snprintf(names[1], sizeof(names[1]), "%s", renamed_to);
    if (mutation_refresh_fails) open_failures = 1;
    return true;
}
static const t5_storage_api_v1 storage_api = {
    .api_version = T5_STORAGE_API_VERSION, .struct_size = sizeof(t5_storage_api_v1),
    .read_file = read_session, .write_file_atomic = write_session, .remove_file = remove_session,
    .exists = exists, .rename_file = rename_file,
};
const t5_storage_api_v1 *t5_storage_get_api(uint32_t v) { assert(v == T5_STORAGE_API_VERSION); return &storage_api; }

static bool keyboard_request(const char *t, const char *i, size_t n, uint8_t k, uint64_t c) {
    (void)t; (void)i; (void)n; (void)k; (void)c; return false;
}
static bool keyboard_take(char *out, size_t cap, bool *cancelled, uint64_t *cookie) {
    if (!have_rename) return false;
    have_rename = false; *cookie = RENAME_COOKIE; *cancelled = cancel_rename;
    snprintf(out, cap, "%s", renamed_to); return true;
}
static void home(void) { ++home_calls; }
static const t5_system_ui_api_v1 system_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION, .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = keyboard_request, .keyboard_take_result = keyboard_take, .navigate_home = home,
};
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t v) { assert(v == T5_SYSTEM_UI_API_VERSION); return &system_api; }

static bool hidden(void) { return false; }
static void render(const char *path, const char *status, const t5_file_browser_entry_t *rows,
                   uint32_t count, int32_t selected) {
    assert(!directory_open); ++render_calls;
    snprintf(shown_path, sizeof(shown_path), "%s", path);
    snprintf(shown_status, sizeof(shown_status), "%s", status);
    shown_count = count; shown_selection = selected;
    for (uint32_t i = 0; i < count; ++i) assert(rows[i].name && rows[i].name[0]);
    if (!start_handler) {
        printf("FRAME path=%s status=%s selected=%ld", path, status, (long)selected);
        for (uint32_t i = 0; i < count; ++i) printf(" |%s/%d", rows[i].name, rows[i].is_directory);
        putchar('\n');
    }
}
static bool poll_browser(t5_file_browser_event_t *event, uint32_t wait, bool root, bool dir) {
    (void)root; (void)dir; assert(wait == 20 && !directory_open);
    memset(event, 0, sizeof(*event));
    if (start_handler) {
        event->type = polls++ == 0 ? T5_FILE_BROWSER_EVENT_ROW : T5_FILE_BROWSER_EVENT_OPEN;
        event->row_index = 1; assert(polls <= 2); return true;
    }
    ++polls; event->type = use_back ? T5_FILE_BROWSER_EVENT_BACK : T5_FILE_BROWSER_EVENT_EXIT;
    return !poll_fails;
}
static uint32_t page_items(void) { return 8; }
static bool confirm_request(const char *n, uint64_t cookie) { (void)n; (void)cookie; return false; }
static bool confirm_take(bool *confirmed, uint64_t *cookie) {
    if (!have_delete) return false;
    have_delete = false; *confirmed = confirm_delete; *cookie = HANDOFF_COOKIE; return true;
}
static bool delete_file(const char *path) {
    assert(strstr(path, "second.bmp")); ++mutations;
    if (mutation_fails) return false;
    assert(name_count == 3); strcpy(names[1], names[2]); --name_count;
    if (mutation_refresh_fails) open_failures = 1;
    return true;
}
static bool open_document(const char *path) { (void)path; return false; }
static bool launch_request(const char *path, uint64_t cookie) { (void)path; (void)cookie; return false; }
static bool take_result(bool *ready, int32_t *error, uint64_t *cookie) {
    if (!*ready) return false;
    *ready = false; *error = result_error; *cookie = HANDOFF_COOKIE; return true;
}
static bool launch_take(int32_t *error, uint64_t *cookie) { return take_result(&have_launch, error, cookie); }
static const t5_file_browser_api_v1 browser_api = {
    .api_version = T5_FILE_BROWSER_API_VERSION, .struct_size = sizeof(t5_file_browser_api_v1),
    .show_hidden_files = hidden, .render = render, .poll_event = poll_browser, .page_items = page_items,
    .confirm_delete_request = confirm_request, .confirm_delete_take_result = confirm_take,
    .delete_document = delete_file, .open_document = open_document,
    .launch_elf_request = launch_request, .launch_elf_take_result = launch_take,
};
const t5_file_browser_api_v1 *t5_file_browser_get_api(uint32_t v) { assert(v == T5_FILE_BROWSER_API_VERSION); return &browser_api; }
static uint32_t handler_count(const char *path) { assert(strstr(path, "second.bmp")); return 1; }
static bool handler_get(const char *path, uint32_t index, t5_file_handler_t *handler) {
    assert(strstr(path, "second.bmp") && index == 0); memset(handler, 0, sizeof(*handler));
    strcpy(handler->app_id, "image_viewer"); strcpy(handler->display_name, "Image Viewer"); return true;
}
static bool open_request(const char *path, const char *id, uint64_t cookie) {
    assert(strstr(path, "second.bmp") && !strcmp(id, "image_viewer") && cookie == HANDOFF_COOKIE);
    assert(session_present); launch_requested = true; return true;
}
static bool open_take(int32_t *error, uint64_t *cookie) { return take_result(&have_open, error, cookie); }
static const t5_file_open_api_v1 open_api = {
    .api_version = T5_FILE_OPEN_API_VERSION, .struct_size = sizeof(t5_file_open_api_v1),
    .handler_count = handler_count, .handler_get = handler_get, .open_request = open_request, .open_take_result = open_take,
};
const t5_file_open_api_v1 *t5_file_open_get_api(uint32_t v) { assert(v == T5_FILE_OPEN_API_VERSION); return &open_api; }
static void list_render(const t5_ui_chrome_t *c, const t5_ui_list_row_t *r, uint32_t n, int32_t s) { (void)c; (void)r; (void)n; (void)s; assert(0); }
static bool list_poll(t5_ui_event_t *e, uint32_t wait) { (void)e; (void)wait; assert(0); return false; }
static int32_t list_hit(int16_t x, int16_t y) { (void)x; (void)y; return -1; }
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION, .struct_size = sizeof(t5_ui_api_v1),
    .render_list = list_render, .poll_event = list_poll, .hit_test = list_hit,
    .next_index = next_index, .previous_index = previous_index,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t v) { assert(v == T5_UI_API_VERSION); return &ui_api; }

static bool volume_ready(void *ctx) { (void)ctx; assert(lease_live); return usb_ready; }
static bool volume_refresh(void *ctx) {
    ++refresh_calls;
    if (refresh_failures) { --refresh_failures; return false; }
    return volume_ready(ctx);
}
static bool volume_stat(void *ctx, const char *p, uint64_t *s, bool *d) { (void)ctx; (void)p; (void)s; (void)d; return false; }
static risc_storage_dir_t volume_open(void *ctx, const char *path) { (void)ctx; return open_directory(path, true) ? 7 : 0; }
static bool volume_next(void *ctx, risc_storage_dir_t h, risc_storage_dirent_v1 *out) {
    (void)ctx; assert(h == 7); bool is_dir = false;
    const bool okay = next_directory(out->name, sizeof(out->name), &is_dir);
    if (okay) out->is_directory = is_dir;
    return okay;
}
static void volume_close(void *ctx, risc_storage_dir_t h) { (void)ctx; assert(h == 7); close_directory(); }
static risc_storage_file_t volume_read_open(void *c, const char *p, uint64_t *s) { (void)c; (void)p; (void)s; return 0; }
static risc_storage_file_t volume_write_open(void *c, const char *p) { (void)c; (void)p; return 0; }
static size_t volume_read(void *c, risc_storage_file_t f, void *b, size_t n) { (void)c; (void)f; (void)b; (void)n; return 0; }
static size_t volume_write(void *c, risc_storage_file_t f, const void *b, size_t n) { (void)c; (void)f; (void)b; (void)n; return 0; }
static bool volume_file_close(void *c, risc_storage_file_t f, bool commit) { (void)c; (void)f; (void)commit; return true; }
static bool volume_remove(void *c, const char *p) { (void)c; return delete_file(p); }
static const risc_storage_volume_api_v1 volume_api = {
    .api_version = RISC_STORAGE_VOLUME_API_V1, .struct_size = sizeof(risc_storage_volume_api_v1),
    .refresh = volume_refresh, .ready = volume_ready, .stat = volume_stat,
    .dir_open = volume_open, .dir_next = volume_next, .dir_close = volume_close,
    .file_open_read = volume_read_open, .file_read = volume_read, .file_open_write = volume_write_open,
    .file_write = volume_write, .file_close = volume_file_close, .remove = volume_remove,
};
static bool acquire(const char *cap, uint32_t ver, t5_provider_capability_lease_t *lease, const void **api) {
    assert(!strcmp(cap, "storage.volume") && ver == 1 && !lease_live); ++acquisitions;
    lease_live = true; *lease = 42; *api = &volume_api; return true;
}
static bool release(t5_provider_capability_lease_t lease) { assert(lease == 42 && lease_live && !directory_open); lease_live = false; ++releases; return true; }
static const t5_provider_capability_api_v1 provider_api = {
    .api_version = T5_PROVIDER_CAPABILITY_API_VERSION, .struct_size = sizeof(t5_provider_capability_api_v1),
    .acquire = acquire, .release = release,
};
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v) { assert(v == T5_PROVIDER_CAPABILITY_API_VERSION); return usb_enabled ? &provider_api : NULL; }

static void counters(void) { opens = closes = nexts = render_calls = polls = back_on = back_off = removes = sd_opens = usb_opens = mutations = acquisitions = releases = home_calls = refresh_calls = 0; }
static void reset(void) {
    assert(!directory_open && !lease_live); counters();
    session_present = session_read_fails = session_remove_fails = have_open = have_launch = have_rename = have_delete = cancel_rename = confirm_delete = false;
    mutation_fails = destination_exists = usb_enabled = launch_requested = start_handler = use_back = poll_fails = false;
    mutation_refresh_fails = false; refresh_failures = 0;
    usb_ready = true; result_error = open_failures = 0; stop_after = -1; failure_path = NULL;
    name_count = 3; strcpy(names[0], "first.bmp"); strcpy(names[1], "second.bmp"); strcpy(names[2], "third.bmp");
    strcpy(renamed_to, "renamed.bmp");
}
static void saved(const char *path, const char *name, const char *pending, char volume) {
    session_bytes = (size_t)snprintf(session, sizeof(session), "%s\n%s\n%s\n%c\n", path, name, pending, volume);
    session_present = true;
}
static void run(void) {
    app_main(); assert(!directory_open && back_on == 1 && back_off == 1);
    // The native host owns invocation leases; model its existing teardown boundary.
    if (lease_live) assert(release(42));
    assert(acquisitions == releases);
}
static void expected(const char *path, int selected, unsigned scans, unsigned count) {
    assert(!strcmp(shown_path, path) && shown_selection == selected && shown_count == count);
#ifndef OBSERVE_BASELINE
    assert(opens == scans);
#else
    (void)scans;
#endif
    assert(closes <= opens && !directory_open && render_calls == 1 && polls == 1);
}
int main(void) {
    for (int result = 0; result < 3; ++result) for (int nested = 0; nested < 2; ++nested) {
        reset(); saved(nested ? "/Books" : "/", "second.bmp", "", 'S'); have_open = result == 1; have_launch = result == 2;
        run(); expected(nested ? "/Books" : "/", 1, 1, 3); assert(!shown_status[0] && !session_present);
    }
    // Real outgoing save-session/handler request, then a new invocation with changed contents.
    reset(); start_handler = true; run(); assert(launch_requested && session_present);
    assert(!strncmp(session, "/\nsecond.bmp\n", 13));
    counters(); start_handler = false; have_open = true; strcpy(names[0], "added.bmp");
    run(); expected("/", 1, 1, 3); assert(!session_present && !have_open);
    // Saved name disappeared: retain the existing first-entry fallback, never stale rows.
    reset(); saved("/Books", "gone.bmp", "", 'S'); have_open = true; run(); expected("/Books", 0, 1, 3);
    for (int raw = 0; raw < 2; ++raw) {
        reset(); saved("/Books", "second.bmp", "", 'S'); have_launch = raw; have_open = !raw; result_error = -7;
        run(); expected("/Books", 1, 1, 3); assert(strstr(shown_status, raw ? "Native app failed: -7" : "File handler failed: -7"));
        reset(); saved("/Books", "second.bmp", "", 'S'); have_launch = raw; have_open = !raw; open_failures = 1; failure_path = "/sd/Books";
        run(); expected("/Books", -1, 2, 3); assert(closes == opens - 1);
        reset(); saved("/Books", "second.bmp", "", 'S'); have_launch = raw; have_open = !raw; open_failures = 10; failure_path = "/sd/Books";
        run(); expected("/Books", -1, 2, 0); assert(closes == opens - 2);
    }
    reset(); have_open = true; open_failures = 1; run(); expected("/", -1, 2, 3);
    // Missing/read-failed/truncated sessions all use the ordinary fresh-root path.
    const char *bad[] = {"", "/Books", "/Books\nsecond.bmp"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        reset(); strcpy(session, bad[i]); session_bytes = strlen(session); session_present = true;
        run(); expected("/", -1, 1, 3);
    }
    reset(); saved("/Books", "second.bmp", "", 'S'); session_read_fails = true; run(); expected("/", -1, 1, 3);
    reset(); run(); expected("/", -1, 1, 3);
    reset(); saved("relative", "second.bmp", "", 'S'); run(); expected("/", 1, 1, 3);
    reset(); saved("/Books", "second.bmp", "", 'S'); name_count = 0; have_open = true; run(); expected("/Books", -1, 1, 0);
    // No saved-name truncation may turn a nonmatching session name into a match.
    reset(); char long_name[300]; memset(long_name, 'z', sizeof(long_name) - 1); long_name[299] = 0;
    memset(names[1], 'z', sizeof(names[1]) - 1); names[1][sizeof(names[1]) - 1] = 0;
    saved("/Books", long_name, "", 'S'); run(); expected("/Books", 0, 1, 3);
    // Mutating continuations still re-list, including failed/cancelled deletion.
    for (int fail = 0; fail < 2; ++fail) {
        reset(); saved("/Books", "second.bmp", "/Books/second.bmp", 'S'); have_delete = confirm_delete = true; mutation_fails = fail;
        run(); expected("/Books", 1, 2, fail ? 3 : 2); assert(mutations == 1);
        reset(); saved("/Books", "second.bmp", "", 'S'); have_rename = true; mutation_fails = fail;
        run(); expected("/Books", 1, fail ? 1 : 2, 3); assert(mutations == 1);
        assert(strstr(shown_status, fail ? "Rename failed" : "Renamed to renamed.bmp"));
    }
    reset(); saved("/Books", "second.bmp", "/Books/second.bmp", 'S'); have_delete = true; run(); expected("/Books", 1, 2, 3); assert(mutations == 0);
    reset(); saved("/Books", "second.bmp", "", 'S'); have_rename = cancel_rename = true; run(); expected("/Books", 1, 1, 3); assert(mutations == 0);
    // A successful mutation's fresh listing also satisfies a co-present copied result.
    reset(); saved("/Books", "second.bmp", "", 'S'); have_rename = have_open = have_launch = true; run(); expected("/Books", 1, 2, 3);
    reset(); saved("/Books", "second.bmp", "", 'S'); have_rename = have_open = mutation_refresh_fails = true;
    run(); expected("/Books", 0, 3, 3); assert(mutations == 1);
    reset(); saved("/Books", "second.bmp", "/Books/second.bmp", 'S'); have_delete = confirm_delete = have_open = mutation_refresh_fails = true;
    run(); expected("/Books", 0, 3, 2); assert(mutations == 1);
    reset(); saved("/", "second.bmp", "", 'S'); have_rename = true; open_failures = 1;
    run(); expected("/", 1, 3, 3); assert(mutations == 1 && !strcmp(names[1], "renamed.bmp"));
    for (int fail = 0; fail < 2; ++fail) {
        reset(); saved("/USB Storage/Books", "second.bmp", "", 'U'); usb_enabled = have_launch = true; open_failures = fail; failure_path = "/Books";
        run(); expected("/USB Storage/Books", fail ? -1 : 1, fail ? 2 : 1, 3); 
#ifndef OBSERVE_BASELINE
        assert(sd_opens == 0 && usb_opens == (unsigned)(fail ? 2 : 1));
#endif
    }
    reset(); saved("/USB Storage/Books", "second.bmp", "", 'U'); usb_enabled = have_open = true; usb_ready = false;
    run(); expected("/USB Storage/Books", -1, 0, 0); assert(strstr(shown_status, "disconnected"));
    reset(); saved("/USB Storage/Books", "second.bmp", "", 'U'); have_open = true;
    run(); expected("/USB Storage/Books", -1, 0, 0); assert(acquisitions == 0);
    // Arrival during the second bounded provider poll needs no SD-root enumeration.
    reset(); saved("/USB Storage/Books", "second.bmp", "", 'U'); usb_enabled = true; refresh_failures = 1;
    run(); expected("/USB Storage/Books", 1, 1, 3); assert(refresh_calls == 2 && !shown_status[0]);
    reset(); saved("/USB Storage/Books", "second.bmp", "/second.bmp", 'U'); usb_enabled = have_delete = confirm_delete = true;
    run(); expected("/USB Storage/Books", 1, 2, 2); assert(mutations == 1);
#ifndef OBSERVE_BASELINE
    assert(sd_opens == 0);
#endif
    // Session cleanup errors do not retain directory handles or prevent the next fresh invocation.
    reset(); saved("/Books", "second.bmp", "", 'S'); have_open = true; session_remove_fails = true;
    run(); expected("/Books", 1, 1, 3); assert(session_present);
    counters(); session_remove_fails = false; run(); expected("/Books", 1, 1, 3); assert(!session_present);
    // The v1 iterator's false result is EOF/error-indistinguishable; no cache persists on retry.
    reset(); saved("/Books", "second.bmp", "", 'S'); stop_after = 0; run(); expected("/Books", -1, 1, 0);
    counters(); stop_after = -1; run(); expected("/", -1, 1, 3);
    reset(); use_back = true; run(); expected("/", -1, 1, 3); assert(home_calls == 1);
    reset(); poll_fails = true; run(); expected("/", -1, 1, 3);
    puts("File Browser resume behavior, bounded retries, mutation refresh and cleanup PASS");
}
