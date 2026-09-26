#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5FileBrowserApi.h"
#include "T5FileOpenApi.h"
#include "T5UiApi.h"
#include "T5StorageApi.h"
#include "T5SystemUiApi.h"

void app_main(void);

static int phase;
static int event_index;
static int render_index;
static int dir_index;
static char open_dir[64];
static char session_data[1400];
static size_t session_size;
static bool session_exists;
static bool book2_deleted;
static bool confirm_available;
static bool delete_requested;
static int back_exit_false_count;
static int back_exit_true_count;
static int ui_event_index;
static int chooser_renders;
static bool open_requested;

static const t5_app_dirent_t root_entries[] = {
    {.name = "Book10.epub", .is_directory = 0},
    {.name = ".hidden.epub", .is_directory = 0},
    {.name = "notes.md", .is_directory = 0},
    {.name = "Book2.epub", .is_directory = 0},
    {.name = "game.elf", .is_directory = 0},
    {.name = "image.bmp", .is_directory = 0},
    {.name = "ignore.pdf", .is_directory = 0},
    {.name = "System Volume Information", .is_directory = 1},
    {.name = "Books", .is_directory = 1},
};
static const t5_app_dirent_t books_entries[] = {
    {.name = "Child10.txt", .is_directory = 0},
    {.name = "Child2.txt", .is_directory = 0},
};

static bool dir_open(const char *path) {
    assert(path);
    assert(strcmp(path, "/sd") == 0 || strcmp(path, "/sd/Books") == 0);
    snprintf(open_dir, sizeof(open_dir), "%s", path);
    dir_index = 0;
    return true;
}

static bool dir_next(t5_app_dirent_t *entry) {
    assert(entry);
    if (strcmp(open_dir, "/sd/Books") == 0) {
        if (dir_index >= (int)(sizeof(books_entries) / sizeof(books_entries[0]))) return false;
        *entry = books_entries[dir_index++];
        return true;
    }
    while (dir_index < (int)(sizeof(root_entries) / sizeof(root_entries[0]))) {
        *entry = root_entries[dir_index++];
        if (book2_deleted && strcmp(entry->name, "Book2.epub") == 0) continue;
        return true;
    }
    return false;
}

static void dir_close(void) { open_dir[0] = 0; }
static void set_back_exits(bool enabled) {
    if (enabled) ++back_exit_true_count;
    else ++back_exit_false_count;
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .dir_open = dir_open,
    .dir_next = dir_next,
    .dir_close = dir_close,
    .set_back_exits_app = set_back_exits,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

static bool storage_read(const char *path, void *buffer, size_t capacity, size_t *size_out) {
    assert(strcmp(path, "/sd/System/State/FileBrowser/Session.txt") == 0);
    if (!session_exists) return false;
    if (size_out) *size_out = session_size;
    if (!buffer || capacity == 0) return true;
    if (capacity < session_size) return false;
    memcpy(buffer, session_data, session_size);
    return true;
}

static bool storage_write(const char *path, const void *data, size_t size) {
    assert(strcmp(path, "/sd/System/State/FileBrowser/Session.txt") == 0);
    assert(data && size < sizeof(session_data));
    memcpy(session_data, data, size);
    session_size = size;
    session_exists = true;
    return true;
}

static bool storage_remove(const char *path) {
    assert(strcmp(path, "/sd/System/State/FileBrowser/Session.txt") == 0);
    session_exists = false;
    session_size = 0;
    return true;
}

static const t5_storage_api_v1 storage_api = {
    .api_version = T5_STORAGE_API_VERSION,
    .struct_size = sizeof(t5_storage_api_v1),
    .read_file = storage_read,
    .write_file_atomic = storage_write,
    .remove_file = storage_remove,
};

const t5_storage_api_v1 *t5_storage_get_api(uint32_t version) {
    return version == T5_STORAGE_API_VERSION ? &storage_api : NULL;
}

static void navigate_home(void) { assert(!"File Browser unexpectedly navigated Home"); }
static const t5_system_ui_api_v1 system_ui_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .navigate_home = navigate_home,
};

const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &system_ui_api : NULL;
}

static bool show_hidden(void) { return false; }

static void assert_entry(const t5_file_browser_entry_t *entries, uint32_t index,
                         const char *name, bool is_directory) {
    assert(entries[index].name);
    assert(strcmp(entries[index].name, name) == 0);
    assert(entries[index].is_directory == is_directory);
}

static void render_browser(const char *path, const char *status,
                           const t5_file_browser_entry_t *entries,
                           uint32_t count, int32_t selected) {
    assert(path && status);
    if (phase != 3 || render_index == 0) assert(status[0] == 0);
    if (phase == 1) {
        if (render_index == 0) {
            assert(strcmp(path, "/") == 0 && count == 7 && selected == 0);
            assert_entry(entries, 0, "Books", true);
            assert_entry(entries, 1, "Book2.epub", false);
            assert_entry(entries, 2, "Book10.epub", false);
            assert_entry(entries, 3, "game.elf", false);
            assert_entry(entries, 4, "ignore.pdf", false);
            assert_entry(entries, 5, "image.bmp", false);
            assert_entry(entries, 6, "notes.md", false);
        } else if (render_index == 1) {
            assert(strcmp(path, "/Books") == 0 && count == 2 && selected == 0);
            assert_entry(entries, 0, "Child2.txt", false);
            assert_entry(entries, 1, "Child10.txt", false);
        } else if (render_index == 2) {
            assert(strcmp(path, "/") == 0 && count == 7 && selected == 0);
            assert_entry(entries, 0, "Books", true);
        } else if (render_index == 3) {
            assert(strcmp(path, "/") == 0 && count == 7 && selected == 1);
            assert_entry(entries, 1, "Book2.epub", false);
            assert_entry(entries, 4, "ignore.pdf", false);
        } else {
            assert(!"unexpected phase-1 render");
        }
    } else if (phase == 2) {
        assert(render_index == 0);
        assert(strcmp(path, "/") == 0 && count == 6 && selected == 1);
        assert_entry(entries, 0, "Books", true);
        assert_entry(entries, 1, "Book10.epub", false);
        assert_entry(entries, 2, "game.elf", false);
        assert_entry(entries, 3, "ignore.pdf", false);
    } else if (phase == 3) {
        assert(strcmp(path, "/") == 0 && count == 6);
        if (render_index == 0) {
            assert(status[0] == 0);
        } else if (render_index == 1) {
            assert(strstr(status, "No registered app") != NULL);
            assert(selected == 3);
        } else {
            assert(!"unexpected phase-3 render");
        }
    } else {
        assert(phase == 4 && render_index == 0);
        assert(strcmp(path, "/") == 0 && count == 6 && status[0] == 0);
        assert_entry(entries, 5, "notes.md", false);
    }
    ++render_index;
}

static bool poll_browser(t5_file_browser_event_t *event, uint32_t wait_ms,
                         bool at_root, bool selected_is_directory) {
    assert(event && wait_ms == 20);
    memset(event, 0, sizeof(*event));
    event->row_index = -1;
    if (phase == 1) {
        switch (event_index++) {
            case 0:
                assert(at_root && selected_is_directory);
                event->type = T5_FILE_BROWSER_EVENT_ROW;
                event->row_index = 0;
                return true;
            case 1:
                assert(!at_root && !selected_is_directory);
                event->type = T5_FILE_BROWSER_EVENT_BACK;
                return true;
            case 2:
                assert(at_root && selected_is_directory);
                event->type = T5_FILE_BROWSER_EVENT_NEXT;
                return true;
            case 3:
                assert(at_root && !selected_is_directory);
                event->type = T5_FILE_BROWSER_EVENT_DELETE;
                return true;
            default:
                assert(!"unexpected phase-1 event poll");
        }
    }
    if (phase == 2) {
        assert(event_index++ == 0);
        assert(at_root && !selected_is_directory);
        event->type = T5_FILE_BROWSER_EVENT_EXIT;
        return true;
    }
    if (phase == 3) {
        assert(at_root);
        if (event_index++ == 0) {
            event->type = T5_FILE_BROWSER_EVENT_ROW;
            event->row_index = 3; /* ignore.pdf after Book2 is deleted */
        } else {
            event->type = T5_FILE_BROWSER_EVENT_EXIT;
        }
        return true;
    }
    assert(phase == 4 && event_index++ == 0 && at_root);
    event->type = T5_FILE_BROWSER_EVENT_ROW;
    event->row_index = 5; /* notes.md */
    return true;
}

static uint32_t page_items(void) { return 4; }

static bool confirm_request(const char *name, uint64_t cookie) {
    assert(phase == 1);
    assert(strcmp(name, "Book2.epub") == 0);
    assert(cookie == 0x4642524f57534552ULL);
    assert(session_exists);
    assert(strstr(session_data, "/\nBook2.epub\n/Book2.epub\n") != NULL);
    delete_requested = true;
    confirm_available = true;
    return true;
}

static bool confirm_take(bool *confirmed, uint64_t *cookie) {
    if (phase != 2 || !confirm_available) return false;
    confirm_available = false;
    if (confirmed) *confirmed = true;
    if (cookie) *cookie = 0x4642524f57534552ULL;
    return true;
}

static bool delete_document(const char *path) {
    assert(phase == 2);
    assert(strcmp(path, "/Book2.epub") == 0);
    book2_deleted = true;
    return true;
}

static bool open_document(const char *path) {
    (void)path;
    assert(!"unexpected document open");
    return false;
}

static bool launch_request(const char *path, uint64_t cookie) {
    (void)path;
    (void)cookie;
    assert(!"unexpected ELF launch");
    return false;
}

static bool launch_take(int32_t *error, uint64_t *cookie) {
    (void)error;
    (void)cookie;
    return false;
}

static uint32_t handler_count(const char *path) {
    if (strstr(path, "notes.md")) return 2;
    if (strstr(path, ".epub")) return 1;
    if (strstr(path, ".txt")) return 2;
    if (strstr(path, ".bmp")) return 1;
    return 0;
}
static bool handler_get(const char *path, uint32_t index, t5_file_handler_t *out) {
    assert(path && out);
    memset(out, 0, sizeof(*out));
    if (strstr(path, "notes.md") || strstr(path, ".txt")) {
        assert(index < 2);
        if (index == 0) {
            out->kind = T5_FILE_HANDLER_SYSTEM_READER;
            strcpy(out->app_id, "riscrte-reader");
            strcpy(out->display_name, "Reader");
        } else {
            out->kind = T5_FILE_HANDLER_APP;
            strcpy(out->app_id, "text_editor");
            strcpy(out->display_name, "Text Editor");
        }
        return true;
    }
    if (index == 0 && strstr(path, ".epub")) {
        out->kind = T5_FILE_HANDLER_SYSTEM_READER;
        strcpy(out->app_id, "riscrte-reader");
        strcpy(out->display_name, "Reader");
        return true;
    }
    if (index == 0 && strstr(path, ".bmp")) {
        out->kind = T5_FILE_HANDLER_APP;
        strcpy(out->app_id, "image_viewer");
        strcpy(out->display_name, "Image Viewer");
        return true;
    }
    return false;
}
static bool open_request(const char *path, const char *app_id, uint64_t cookie) {
    assert(phase == 4);
    assert(!strcmp(path, "/sd/notes.md"));
    assert(!strcmp(app_id, "text_editor"));
    assert(cookie == 0x4642524f57534552ULL);
    open_requested = true;
    return true;
}
static bool open_take_result(int32_t *error, uint64_t *cookie) {
    (void)error; (void)cookie; return false;
}
static bool file_refresh(void) { return true; }
static bool source_path_get(char *out, size_t capacity) {
    if (out && capacity) out[0] = 0;
    return false;
}
static const t5_file_open_api_v1 file_open_api = {
    T5_FILE_OPEN_API_VERSION, sizeof(t5_file_open_api_v1),
    file_refresh, handler_count, handler_get, open_request, open_take_result, source_path_get,
};
const t5_file_open_api_v1 *t5_file_open_get_api(uint32_t version) {
    return version == T5_FILE_OPEN_API_VERSION ? &file_open_api : NULL;
}

static void ui_render(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                      uint32_t count, int32_t selected) {
    assert(phase == 4 && chrome && rows && count == 2);
    assert(!strcmp(chrome->title, "Open with"));
    assert(!strcmp(rows[0].title, "Reader"));
    assert(!strcmp(rows[1].title, "Text Editor"));
    assert(selected >= 0 && selected < 2);
    ++chooser_renders;
}
static bool ui_poll(t5_ui_event_t *event, uint32_t wait_ms) {
    assert(phase == 4 && event && wait_ms == 20);
    memset(event, 0, sizeof(*event));
    event->type = ui_event_index++ == 0 ? T5_UI_EVENT_NEXT : T5_UI_EVENT_CONFIRM;
    return true;
}
static int32_t ui_hit(int16_t x, int16_t y) { (void)x; (void)y; return -1; }
static int32_t ui_next(int32_t current, uint32_t count) {
    return count ? (current + 1) % (int32_t)count : 0;
}
static int32_t ui_previous(int32_t current, uint32_t count) {
    return count ? (current + (int32_t)count - 1) % (int32_t)count : 0;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = ui_render,
    .hit_test = ui_hit,
    .poll_event = ui_poll,
    .next_index = ui_next,
    .previous_index = ui_previous,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

static const t5_file_browser_api_v1 browser_api = {
    .api_version = T5_FILE_BROWSER_API_VERSION,
    .struct_size = sizeof(t5_file_browser_api_v1),
    .show_hidden_files = show_hidden,
    .render = render_browser,
    .poll_event = poll_browser,
    .page_items = page_items,
    .confirm_delete_request = confirm_request,
    .confirm_delete_take_result = confirm_take,
    .delete_document = delete_document,
    .open_document = open_document,
    .launch_elf_request = launch_request,
    .launch_elf_take_result = launch_take,
};

const t5_file_browser_api_v1 *t5_file_browser_get_api(uint32_t version) {
    return version == T5_FILE_BROWSER_API_VERSION ? &browser_api : NULL;
}

int main(void) {
    phase = 1;
    event_index = render_index = 0;
    app_main();
    assert(delete_requested);
    assert(session_exists);
    assert(!book2_deleted);
    assert(render_index == 4);

    phase = 2;
    event_index = render_index = 0;
    app_main();
    assert(book2_deleted);
    assert(!session_exists);
    assert(render_index == 1);
    assert(back_exit_false_count == 2);
    assert(back_exit_true_count == 2);

    phase = 3;
    event_index = render_index = 0;
    app_main();
    assert(render_index == 2);
    assert(!open_requested);

    phase = 4;
    event_index = render_index = ui_event_index = chooser_renders = 0;
    app_main();
    assert(open_requested);
    assert(chooser_renders >= 2);
    assert(session_exists);
    assert(back_exit_false_count == 4);
    assert(back_exit_true_count == 4);
    return 0;
}
