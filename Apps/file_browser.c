#include "T5AppApi.h"
#include "T5FileBrowserApi.h"
#include "T5ImageApi.h"
#include "T5StorageApi.h"
#include "T5SystemUiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ENTRIES 256
#define PATH_CAP 512
#define STATUS_CAP 128
#define SESSION_PATH "/sd/.crosspoint/file_browser.session"
#define HANDOFF_COOKIE 0x4642524f57534552ULL

typedef struct {
    char name[T5_APP_DIRENT_NAME_MAX];
    bool is_directory;
} browser_entry_t;

static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_system_ui_api_v1 *system_ui;
static const t5_file_browser_api_v1 *browser;
static const t5_image_api_v1 *image;

static browser_entry_t entries[MAX_ENTRIES];
static t5_file_browser_entry_t ui_entries[MAX_ENTRIES];
static uint32_t entry_count;
static int32_t selected_index;
static char base_path[PATH_CAP] = "/";
static char status_text[STATUS_CAP];
static char pending_delete_path[PATH_CAP];

static char lower_ascii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
}

static void copy_text(char *dst, size_t capacity, const char *src) {
    size_t i = 0;
    if (!dst || capacity == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static bool ends_with_ci(const char *value, const char *suffix) {
    size_t n = value ? strlen(value) : 0;
    size_t s = suffix ? strlen(suffix) : 0;
    if (n < s) return false;
    for (size_t i = 0; i < s; ++i) {
        if (lower_ascii(value[n - s + i]) != lower_ascii(suffix[i])) return false;
    }
    return true;
}

static bool image_file(const char *name) {
    return ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg") ||
           ends_with_ci(name, ".png") || ends_with_ci(name, ".bmp");
}

static bool supported_file(const char *name) {
    return ends_with_ci(name, ".epub") || ends_with_ci(name, ".xtc") || ends_with_ci(name, ".xtch") ||
           ends_with_ci(name, ".txt") || ends_with_ci(name, ".md") || image_file(name) ||
           ends_with_ci(name, ".elf");
}

static int natural_compare(const browser_entry_t *a, const browser_entry_t *b) {
    if (a->is_directory != b->is_directory) return a->is_directory ? -1 : 1;
    const char *s1 = a->name;
    const char *s2 = b->name;
    while (*s1 && *s2) {
        if (*s1 >= '0' && *s1 <= '9' && *s2 >= '0' && *s2 <= '9') {
            while (*s1 == '0') ++s1;
            while (*s2 == '0') ++s2;
            int len1 = 0, len2 = 0;
            while (s1[len1] >= '0' && s1[len1] <= '9') ++len1;
            while (s2[len2] >= '0' && s2[len2] <= '9') ++len2;
            if (len1 != len2) return len1 < len2 ? -1 : 1;
            for (int i = 0; i < len1; ++i) {
                if (s1[i] != s2[i]) return s1[i] < s2[i] ? -1 : 1;
            }
            s1 += len1;
            s2 += len2;
        } else {
            char c1 = lower_ascii(*s1++);
            char c2 = lower_ascii(*s2++);
            if (c1 != c2) return c1 < c2 ? -1 : 1;
        }
    }
    if (!*s1 && *s2) return -1;
    if (*s1 && !*s2) return 1;
    return 0;
}

static void sort_entries(void) {
    for (uint32_t i = 1; i < entry_count; ++i) {
        browser_entry_t item = entries[i];
        uint32_t j = i;
        while (j > 0 && natural_compare(&item, &entries[j - 1]) < 0) {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = item;
    }
}

static void rebuild_ui_entries(void) {
    for (uint32_t i = 0; i < entry_count; ++i) {
        ui_entries[i].name = entries[i].name;
        ui_entries[i].is_directory = entries[i].is_directory;
    }
}

static void make_vfs_dir(char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    if (strcmp(base_path, "/") == 0) copy_text(out, capacity, "/sd");
    else {
        copy_text(out, capacity, "/sd");
        size_t used = strlen(out);
        copy_text(out + used, capacity - used, base_path);
    }
}

static void make_storage_path(const char *name, char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    if (strcmp(base_path, "/") == 0) {
        copy_text(out, capacity, "/");
        copy_text(out + strlen(out), capacity - strlen(out), name);
    } else {
        copy_text(out, capacity, base_path);
        size_t used = strlen(out);
        copy_text(out + used, capacity - used, "/");
        used = strlen(out);
        copy_text(out + used, capacity - used, name);
    }
}

static void make_vfs_path(const char *name, char *out, size_t capacity) {
    char storage_path[PATH_CAP];
    make_storage_path(name, storage_path, sizeof(storage_path));
    copy_text(out, capacity, "/sd");
    size_t used = strlen(out);
    copy_text(out + used, capacity - used, storage_path);
}

static int32_t find_entry(const char *name) {
    if (!name || !name[0]) return 0;
    for (uint32_t i = 0; i < entry_count; ++i) {
        if (strcmp(entries[i].name, name) == 0) return (int32_t)i;
    }
    return 0;
}

static bool load_files(const char *preserve_name) {
    char dir[PATH_CAP + 4];
    t5_app_dirent_t item;
    const bool show_hidden = browser->show_hidden_files();
    make_vfs_dir(dir, sizeof(dir));
    entry_count = 0;
    if (!app->dir_open(dir)) return false;
    while (entry_count < MAX_ENTRIES && app->dir_next(&item)) {
        if ((!show_hidden && item.name[0] == '.') || strcmp(item.name, "System Volume Information") == 0) continue;
        if (!item.is_directory && !supported_file(item.name)) continue;
        copy_text(entries[entry_count].name, sizeof(entries[entry_count].name), item.name);
        entries[entry_count].is_directory = item.is_directory != 0;
        ++entry_count;
    }
    app->dir_close();
    sort_entries();
    rebuild_ui_entries();
    if (entry_count == 0) selected_index = 0;
    else if (preserve_name && preserve_name[0]) selected_index = find_entry(preserve_name);
    else if (selected_index < 0 || selected_index >= (int32_t)entry_count) selected_index = 0;
    return true;
}

static void selected_name(char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    out[0] = 0;
    if (entry_count && selected_index >= 0 && selected_index < (int32_t)entry_count)
        copy_text(out, capacity, entries[selected_index].name);
}

static void save_session(void) {
    char selected[T5_APP_DIRENT_NAME_MAX] = {0};
    char data[PATH_CAP * 2 + T5_APP_DIRENT_NAME_MAX + 8];
    selected_name(selected, sizeof(selected));
    int n = snprintf(data, sizeof(data), "%s\n%s\n%s\n", base_path, selected, pending_delete_path);
    if (n > 0 && (size_t)n < sizeof(data)) storage->write_file_atomic(SESSION_PATH, data, (size_t)n);
}

static void load_session(void) {
    size_t size = 0;
    char data[PATH_CAP * 2 + T5_APP_DIRENT_NAME_MAX + 8];
    if (!storage->read_file(SESSION_PATH, data, sizeof(data) - 1, &size) || size == 0 || size >= sizeof(data)) return;
    data[size] = 0;
    char *line1 = data;
    char *line2 = strchr(line1, '\n');
    if (!line2) return;
    *line2++ = 0;
    char *line3 = strchr(line2, '\n');
    if (!line3) return;
    *line3++ = 0;
    char *line4 = strchr(line3, '\n');
    if (line4) *line4 = 0;
    if (line1[0] == '/') copy_text(base_path, sizeof(base_path), line1);
    copy_text(pending_delete_path, sizeof(pending_delete_path), line3);
    load_files(line2);
}

static void clear_session(void) {
    storage->remove_file(SESSION_PATH);
    pending_delete_path[0] = 0;
}

static int32_t next_index(int32_t current, uint32_t count) {
    return count ? (current + 1) % (int32_t)count : 0;
}

static int32_t previous_index(int32_t current, uint32_t count) {
    return count ? (current + (int32_t)count - 1) % (int32_t)count : 0;
}

static int32_t next_page(int32_t current, uint32_t count, uint32_t page_items) {
    if (!count || !page_items) return 0;
    if (count <= page_items) return next_index(current, count);
    int32_t last_page = ((int32_t)count - 1) / (int32_t)page_items;
    int32_t page = current / (int32_t)page_items;
    return page < last_page ? (page + 1) * (int32_t)page_items : 0;
}

static int32_t previous_page(int32_t current, uint32_t count, uint32_t page_items) {
    if (!count || !page_items) return 0;
    if (count <= page_items) return previous_index(current, count);
    int32_t last_page = ((int32_t)count - 1) / (int32_t)page_items;
    int32_t page = current / (int32_t)page_items;
    return page > 0 ? (page - 1) * (int32_t)page_items : last_page * (int32_t)page_items;
}

static bool go_up(void) {
    if (strcmp(base_path, "/") == 0) return false;
    char old_path[PATH_CAP];
    char child[T5_APP_DIRENT_NAME_MAX];
    copy_text(old_path, sizeof(old_path), base_path);
    char *last = strrchr(old_path, '/');
    copy_text(child, sizeof(child), last ? last + 1 : old_path);
    char *slash = strrchr(base_path, '/');
    if (!slash || slash == base_path) copy_text(base_path, sizeof(base_path), "/");
    else *slash = 0;
    load_files(child);
    return true;
}

static bool open_selected(void) {
    if (!entry_count || selected_index < 0 || selected_index >= (int32_t)entry_count) return false;
    browser_entry_t *entry = &entries[selected_index];
    if (entry->is_directory) {
        if (strcmp(base_path, "/") == 0) {
            copy_text(base_path, sizeof(base_path), "/");
            copy_text(base_path + 1, sizeof(base_path) - 1, entry->name);
        } else {
            size_t used = strlen(base_path);
            copy_text(base_path + used, sizeof(base_path) - used, "/");
            used = strlen(base_path);
            copy_text(base_path + used, sizeof(base_path) - used, entry->name);
        }
        selected_index = 0;
        status_text[0] = 0;
        load_files(NULL);
        return false;
    }
    if (image_file(entry->name)) {
        char vfs_path[PATH_CAP + 4];
        make_vfs_path(entry->name, vfs_path, sizeof(vfs_path));
        pending_delete_path[0] = 0;
        save_session();
        if (image->viewer_open_request(vfs_path, HANDOFF_COOKIE)) return true;
        clear_session();
        copy_text(status_text, sizeof(status_text), "Image Viewer unavailable");
        return false;
    }
    if (ends_with_ci(entry->name, ".elf")) {
        char vfs_path[PATH_CAP + 4];
        make_vfs_path(entry->name, vfs_path, sizeof(vfs_path));
        pending_delete_path[0] = 0;
        save_session();
        if (browser->launch_elf_request(vfs_path, HANDOFF_COOKIE)) return true;
        clear_session();
        copy_text(status_text, sizeof(status_text), "Native app failed");
        return false;
    }
    char document[PATH_CAP];
    make_storage_path(entry->name, document, sizeof(document));
    clear_session();
    if (browser->open_document(document)) return true;
    copy_text(status_text, sizeof(status_text), "Unable to open file");
    return false;
}

static bool request_delete(void) {
    if (!entry_count || selected_index < 0 || selected_index >= (int32_t)entry_count || entries[selected_index].is_directory)
        return false;
    make_storage_path(entries[selected_index].name, pending_delete_path, sizeof(pending_delete_path));
    save_session();
    if (browser->confirm_delete_request(entries[selected_index].name, HANDOFF_COOKIE)) return true;
    clear_session();
    return false;
}

static void consume_handoff_results(void) {
    bool confirmed = false;
    uint64_t cookie = 0;
    if (browser->confirm_delete_take_result(&confirmed, &cookie)) {
        const int32_t old_index = selected_index;
        if (confirmed && pending_delete_path[0]) {
            if (!browser->delete_document(pending_delete_path))
                copy_text(status_text, sizeof(status_text), "Failed to delete file");
            else
                status_text[0] = 0;
        }
        pending_delete_path[0] = 0;
        load_files(NULL);
        if (entry_count == 0) selected_index = 0;
        else if (old_index >= (int32_t)entry_count) selected_index = (int32_t)entry_count - 1;
        else selected_index = old_index;
        clear_session();
    }
    int32_t image_error = 0;
    if (image->viewer_open_take_result(&image_error, &cookie)) {
        char selected[T5_APP_DIRENT_NAME_MAX] = {0};
        selected_name(selected, sizeof(selected));
        if (image_error != 0) snprintf(status_text, sizeof(status_text), "Image Viewer failed: %ld", (long)image_error);
        else status_text[0] = 0;
        load_files(selected);
        clear_session();
    }
    int32_t launch_error = 0;
    if (browser->launch_elf_take_result(&launch_error, &cookie)) {
        char selected[T5_APP_DIRENT_NAME_MAX] = {0};
        selected_name(selected, sizeof(selected));
        if (launch_error != 0) snprintf(status_text, sizeof(status_text), "Native app failed: %ld", (long)launch_error);
        else status_text[0] = 0;
        load_files(selected);
        clear_session();
    }
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    browser = t5_file_browser_get_api(T5_FILE_BROWSER_API_VERSION);
    image = t5_image_get_api(T5_IMAGE_API_VERSION);
    if (!app || !storage || !system_ui || !browser || !image || !app->dir_open || !app->dir_next || !app->dir_close ||
        !app->set_back_exits_app || !storage->read_file || !storage->write_file_atomic || !storage->remove_file ||
        !system_ui->navigate_home || !browser->show_hidden_files || !browser->render || !browser->poll_event ||
        !browser->page_items || !browser->confirm_delete_request || !browser->confirm_delete_take_result ||
        !browser->delete_document || !browser->open_document || !browser->launch_elf_request ||
        !browser->launch_elf_take_result || !image->viewer_open_request || !image->viewer_open_take_result) return;

    app->set_back_exits_app(false);
    status_text[0] = 0;
    pending_delete_path[0] = 0;
    entry_count = 0;
    selected_index = 0;
    copy_text(base_path, sizeof(base_path), "/");

    load_files(NULL);
    load_session();
    consume_handoff_results();
    browser->render(base_path, status_text, ui_entries, entry_count, selected_index);

    for (;;) {
        t5_file_browser_event_t event;
        const bool is_dir = entry_count && selected_index >= 0 && selected_index < (int32_t)entry_count
                                ? entries[selected_index].is_directory : false;
        if (!browser->poll_event(&event, 20, strcmp(base_path, "/") == 0, is_dir)) break;
        bool redraw = false;
        switch (event.type) {
            case T5_FILE_BROWSER_EVENT_PREVIOUS:
                selected_index = previous_index(selected_index, entry_count); redraw = true; break;
            case T5_FILE_BROWSER_EVENT_NEXT:
                selected_index = next_index(selected_index, entry_count); redraw = true; break;
            case T5_FILE_BROWSER_EVENT_PAGE_PREVIOUS:
                selected_index = previous_page(selected_index, entry_count, browser->page_items()); redraw = true; break;
            case T5_FILE_BROWSER_EVENT_PAGE_NEXT:
                selected_index = next_page(selected_index, entry_count, browser->page_items()); redraw = true; break;
            case T5_FILE_BROWSER_EVENT_ROW:
                if (event.row_index >= 0 && event.row_index < (int32_t)entry_count) {
                    selected_index = event.row_index;
                    if (open_selected()) { app->set_back_exits_app(true); return; }
                    redraw = true;
                }
                break;
            case T5_FILE_BROWSER_EVENT_OPEN:
                if (open_selected()) { app->set_back_exits_app(true); return; }
                redraw = true;
                break;
            case T5_FILE_BROWSER_EVENT_DELETE:
                if (request_delete()) { app->set_back_exits_app(true); return; }
                redraw = true;
                break;
            case T5_FILE_BROWSER_EVENT_ROOT:
                copy_text(base_path, sizeof(base_path), "/");
                selected_index = 0;
                status_text[0] = 0;
                load_files(NULL);
                redraw = true;
                break;
            case T5_FILE_BROWSER_EVENT_BACK:
                if (go_up()) redraw = true;
                else {
                    clear_session();
                    system_ui->navigate_home();
                    app->set_back_exits_app(true);
                    return;
                }
                break;
            case T5_FILE_BROWSER_EVENT_EXIT:
                clear_session();
                app->set_back_exits_app(true);
                return;
            default:
                break;
        }
        if (redraw) browser->render(base_path, status_text, ui_entries, entry_count, selected_index);
    }
    app->set_back_exits_app(true);
}
