#include "T5AppApi.h"
#include "T5FileBrowserApi.h"
#include "T5FileOpenApi.h"
#include "T5ProviderCapabilityApi.h"
#include "T5UiApi.h"
#include "T5StorageApi.h"
#include "T5SystemUiApi.h"
#include "RiscStorageVolumeV1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ENTRIES 256
#define PICKER_ENTRIES 96
#define PATH_CAP 512
#define STATUS_CAP 160
#define COPY_CHUNK 4096u
#define SESSION_PATH "/sd/System/State/Applications/file_browser/Session.txt"
#define HANDOFF_COOKIE 0x4642524f57534552ULL
#define USB_ROOT "/USB Storage"
#define USB_ENTRY "USB Storage"

typedef struct {
    char name[T5_APP_DIRENT_NAME_MAX];
    bool is_directory;
    bool usb_root;
} browser_entry_t;

static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_system_ui_api_v1 *system_ui;
static const t5_file_browser_api_v1 *browser;
static const t5_file_open_api_v1 *file_open;
static const t5_ui_api_v1 *ui;
static const t5_provider_capability_api_v1 *providers;
static const risc_storage_volume_api_v1 *usb_volume;
static t5_provider_capability_lease_t usb_lease;

static browser_entry_t entries[MAX_ENTRIES];
static t5_file_browser_entry_t ui_entries[MAX_ENTRIES];
static char picker_names[PICKER_ENTRIES][T5_APP_DIRENT_NAME_MAX];
static t5_ui_list_row_t picker_rows[PICKER_ENTRIES + 2u];
static uint8_t copy_buffer[COPY_CHUNK];
static uint32_t entry_count;
static int32_t selected_index;
static char base_path[PATH_CAP] = "/";
static char status_text[STATUS_CAP];
static char pending_delete_path[PATH_CAP];
static bool pending_delete_usb;

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

static bool using_usb(void) {
    const size_t root_len = sizeof(USB_ROOT) - 1u;
    return strncmp(base_path, USB_ROOT, root_len) == 0 &&
           (base_path[root_len] == 0 || base_path[root_len] == '/');
}

static bool usb_api_valid(const risc_storage_volume_api_v1 *volume) {
    return volume && volume->api_version == RISC_STORAGE_VOLUME_API_V1 &&
           volume->struct_size >= sizeof(risc_storage_volume_api_v1) &&
           volume->refresh && volume->ready && volume->stat &&
           volume->dir_open && volume->dir_next && volume->dir_close &&
           volume->file_open_read && volume->file_read &&
           volume->file_open_write && volume->file_write && volume->file_close &&
           volume->remove;
}

static bool refresh_usb(void) {
    return usb_volume && usb_volume->refresh(usb_volume->context) &&
           usb_volume->ready(usb_volume->context);
}

static void usb_error(const char *fallback) {
    if (usb_volume && usb_volume->last_error &&
        usb_volume->last_error(usb_volume->context, status_text, sizeof(status_text))) return;
    copy_text(status_text, sizeof(status_text), fallback);
}

static int natural_compare(const browser_entry_t *a, const browser_entry_t *b) {
    if (a->usb_root != b->usb_root) return a->usb_root ? -1 : 1;
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

static void make_sd_vfs_dir(const char *relative, char *out, size_t capacity) {
    copy_text(out, capacity, "/sd");
    if (relative && strcmp(relative, "/") != 0) {
        size_t used = strlen(out);
        copy_text(out + used, capacity - used, relative);
    }
}

static void make_vfs_dir(char *out, size_t capacity) {
    make_sd_vfs_dir(base_path, out, capacity);
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

static void make_usb_dir_path(char *out, size_t capacity) {
    const char *relative = base_path + (sizeof(USB_ROOT) - 1u);
    copy_text(out, capacity, relative[0] ? relative : "/");
}

static void make_usb_path(const char *name, char *out, size_t capacity) {
    char dir[PATH_CAP];
    make_usb_dir_path(dir, sizeof(dir));
    if (strcmp(dir, "/") == 0) {
        copy_text(out, capacity, "/");
        copy_text(out + strlen(out), capacity - strlen(out), name);
    } else {
        copy_text(out, capacity, dir);
        size_t used = strlen(out);
        copy_text(out + used, capacity - used, "/");
        used = strlen(out);
        copy_text(out + used, capacity - used, name);
    }
}

static void join_relative_path(const char *directory, const char *name,
                               char *out, size_t capacity) {
    if (strcmp(directory, "/") == 0) {
        copy_text(out, capacity, "/");
        copy_text(out + strlen(out), capacity - strlen(out), name);
    } else {
        copy_text(out, capacity, directory);
        size_t used = strlen(out);
        copy_text(out + used, capacity - used, "/");
        used = strlen(out);
        copy_text(out + used, capacity - used, name);
    }
}

static int32_t find_entry(const char *name) {
    if (!name || !name[0]) return 0;
    for (uint32_t i = 0; i < entry_count; ++i) {
        if (strcmp(entries[i].name, name) == 0) return (int32_t)i;
    }
    return 0;
}

static bool load_usb_files(void) {
    if (!refresh_usb()) {
        entry_count = 0;
        rebuild_ui_entries();
        usb_error("USB storage disconnected");
        return false;
    }
    char path[PATH_CAP];
    make_usb_dir_path(path, sizeof(path));
    risc_storage_dir_t directory = usb_volume->dir_open(usb_volume->context, path);
    if (!directory) {
        entry_count = 0;
        rebuild_ui_entries();
        usb_error("Could not open USB folder");
        return false;
    }
    const bool show_hidden = browser->show_hidden_files();
    risc_storage_dirent_v1 item;
    entry_count = 0;
    while (entry_count < MAX_ENTRIES && usb_volume->dir_next(usb_volume->context, directory, &item)) {
        if ((!show_hidden && item.name[0] == '.') || strcmp(item.name, "System Volume Information") == 0) continue;
        copy_text(entries[entry_count].name, sizeof(entries[entry_count].name), item.name);
        entries[entry_count].is_directory = item.is_directory != 0;
        entries[entry_count].usb_root = false;
        ++entry_count;
    }
    usb_volume->dir_close(usb_volume->context, directory);
    return true;
}

static bool load_sd_files(void) {
    char dir[PATH_CAP + 4];
    t5_app_dirent_t item;
    const bool show_hidden = browser->show_hidden_files();
    make_vfs_dir(dir, sizeof(dir));
    entry_count = 0;
    if (!app->dir_open(dir)) return false;
    while (entry_count < MAX_ENTRIES && app->dir_next(&item)) {
        if ((!show_hidden && item.name[0] == '.') || strcmp(item.name, "System Volume Information") == 0) continue;
        copy_text(entries[entry_count].name, sizeof(entries[entry_count].name), item.name);
        entries[entry_count].is_directory = item.is_directory != 0;
        entries[entry_count].usb_root = false;
        ++entry_count;
    }
    app->dir_close();
    if (strcmp(base_path, "/") == 0 && entry_count < MAX_ENTRIES && refresh_usb()) {
        copy_text(entries[entry_count].name, sizeof(entries[entry_count].name), USB_ENTRY);
        entries[entry_count].is_directory = true;
        entries[entry_count].usb_root = true;
        ++entry_count;
    }
    return true;
}

static bool load_files(const char *preserve_name) {
    const bool okay = using_usb() ? load_usb_files() : load_sd_files();
    sort_entries();
    rebuild_ui_entries();
    if (entry_count == 0) selected_index = 0;
    else if (preserve_name && preserve_name[0]) selected_index = find_entry(preserve_name);
    else if (selected_index < 0 || selected_index >= (int32_t)entry_count) selected_index = 0;
    return okay;
}

static void selected_name(char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    out[0] = 0;
    if (entry_count && selected_index >= 0 && selected_index < (int32_t)entry_count)
        copy_text(out, capacity, entries[selected_index].name);
}

static void save_session(void) {
    char selected[T5_APP_DIRENT_NAME_MAX] = {0};
    char data[PATH_CAP * 2 + T5_APP_DIRENT_NAME_MAX + 16];
    selected_name(selected, sizeof(selected));
    int n = snprintf(data, sizeof(data), "%s\n%s\n%s\n%c\n", base_path, selected,
                     pending_delete_path, pending_delete_usb ? 'U' : 'S');
    if (n > 0 && (size_t)n < sizeof(data)) storage->write_file_atomic(SESSION_PATH, data, (size_t)n);
}

static void load_session(void) {
    size_t size = 0;
    char data[PATH_CAP * 2 + T5_APP_DIRENT_NAME_MAX + 16];
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
    pending_delete_usb = false;
    if (line4) {
        *line4++ = 0;
        char *end = strchr(line4, '\n');
        if (end) *end = 0;
        pending_delete_usb = line4[0] == 'U';
    }
    if (line1[0] == '/') copy_text(base_path, sizeof(base_path), line1);
    copy_text(pending_delete_path, sizeof(pending_delete_path), line3);
    load_files(line2);
}

static void clear_session(void) {
    storage->remove_file(SESSION_PATH);
    pending_delete_path[0] = 0;
    pending_delete_usb = false;
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

static int32_t run_menu(const char *title, const char *subtitle,
                        const t5_ui_list_row_t *rows, uint32_t count) {
    const t5_ui_chrome_t chrome = {
        .title = title, .subtitle = subtitle, .status = "", .back_label = "Cancel",
        .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    int32_t selected = 0;
    ui->render_list(&chrome, rows, count, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20) || event.type == T5_UI_EVENT_BACK ||
            event.type == T5_UI_EVENT_EXIT) return -1;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count);
        else if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count);
        else if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit < 0 || hit >= (int32_t)count) continue;
            return hit;
        } else if (event.type == T5_UI_EVENT_CONFIRM) return selected;
        else continue;
        ui->render_list(&chrome, rows, count, selected);
    }
}

static uint32_t list_picker_directories(bool usb, const char *path, uint32_t offset) {
    uint32_t count = offset;
    const bool show_hidden = browser->show_hidden_files();
    if (usb) {
        if (!refresh_usb()) return count;
        risc_storage_dir_t directory = usb_volume->dir_open(usb_volume->context, path);
        if (!directory) return count;
        risc_storage_dirent_v1 item;
        while (count < PICKER_ENTRIES + 2u && usb_volume->dir_next(usb_volume->context, directory, &item)) {
            if (!item.is_directory || (!show_hidden && item.name[0] == '.') ||
                strcmp(item.name, "System Volume Information") == 0) continue;
            copy_text(picker_names[count - offset], sizeof(picker_names[0]), item.name);
            picker_rows[count] = (t5_ui_list_row_t){picker_names[count - offset], "Folder", "", 0};
            ++count;
        }
        usb_volume->dir_close(usb_volume->context, directory);
    } else {
        char vfs[PATH_CAP + 4];
        make_sd_vfs_dir(path, vfs, sizeof(vfs));
        if (!app->dir_open(vfs)) return count;
        t5_app_dirent_t item;
        while (count < PICKER_ENTRIES + 2u && app->dir_next(&item)) {
            if (!item.is_directory || (!show_hidden && item.name[0] == '.') ||
                strcmp(item.name, "System Volume Information") == 0) continue;
            copy_text(picker_names[count - offset], sizeof(picker_names[0]), item.name);
            picker_rows[count] = (t5_ui_list_row_t){picker_names[count - offset], "Folder", "", 0};
            ++count;
        }
        app->dir_close();
    }
    return count;
}

static bool picker_go_up(char *path) {
    if (!path || strcmp(path, "/") == 0) return false;
    char *slash = strrchr(path, '/');
    if (!slash || slash == path) copy_text(path, PATH_CAP, "/");
    else *slash = 0;
    return true;
}

static bool choose_destination(bool usb, char *out, size_t capacity) {
    char path[PATH_CAP] = "/";
    int32_t selected = 0;
    for (;;) {
        uint32_t count = 0;
        picker_rows[count++] = (t5_ui_list_row_t){"Copy here", usb ? "USB Storage" : "SD Card", path, 0};
        const bool has_parent = strcmp(path, "/") != 0;
        if (has_parent) picker_rows[count++] = (t5_ui_list_row_t){"..", "Parent folder", "", 0};
        const uint32_t directory_start = count;
        count = list_picker_directories(usb, path, directory_start);
        if (usb && !refresh_usb()) {
            usb_error("USB storage disconnected");
            return false;
        }
        if (selected < 0 || selected >= (int32_t)count) selected = 0;
        t5_ui_chrome_t chrome = {
            .title = usb ? "Copy to USB" : "Copy to SD",
            .subtitle = "Choose destination folder", .status = path,
            .back_label = "Cancel", .confirm_label = "Open",
            .previous_label = "Up", .next_label = "Down",
        };
        ui->render_list(&chrome, picker_rows, count, selected);
        for (;;) {
            t5_ui_event_t event = {0};
            if (!ui->poll_event(&event, 20) || event.type == T5_UI_EVENT_BACK ||
                event.type == T5_UI_EVENT_EXIT) return false;
            if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count);
            else if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count);
            else if (event.type == T5_UI_EVENT_TAP) {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit < 0 || hit >= (int32_t)count) continue;
                selected = hit;
                event.type = T5_UI_EVENT_CONFIRM;
            }
            if (event.type == T5_UI_EVENT_CONFIRM) {
                if (selected == 0) {
                    copy_text(out, capacity, path);
                    return true;
                }
                if (has_parent && selected == 1) {
                    picker_go_up(path); selected = 0; break;
                }
                const int32_t directory_index = selected - (int32_t)directory_start;
                if (directory_index < 0 || directory_index >= (int32_t)(count - directory_start)) continue;
                char next[PATH_CAP];
                join_relative_path(path, picker_names[directory_index], next, sizeof(next));
                copy_text(path, sizeof(path), next);
                selected = 0;
                break;
            }
            ui->render_list(&chrome, picker_rows, count, selected);
        }
    }
}

static bool storage_write_stream_available(void) {
    return storage->struct_size >= offsetof(t5_storage_api_v1, write_stream_abort) +
                                   sizeof(storage->write_stream_abort) &&
           storage->stream_open && storage->stream_read && storage->stream_close &&
           storage->write_stream_open && storage->write_stream_write &&
           storage->write_stream_commit && storage->write_stream_abort;
}

static bool copy_sd_to_usb(const char *source_vfs, const char *destination) {
    if (!storage_write_stream_available() || !refresh_usb()) return false;
    size_t source_size = 0;
    t5_storage_stream_t source = storage->stream_open(source_vfs, &source_size);
    if (!source) return false;
    risc_storage_file_t target = usb_volume->file_open_write(usb_volume->context, destination);
    if (!target) {
        storage->stream_close(source);
        usb_error("Could not create USB file");
        return false;
    }
    size_t copied = 0;
    bool okay = true;
    while (copied < source_size) {
        size_t request = source_size - copied;
        if (request > sizeof(copy_buffer)) request = sizeof(copy_buffer);
        size_t got = storage->stream_read(source, copy_buffer, request);
        if (!got || usb_volume->file_write(usb_volume->context, target, copy_buffer, got) != got) {
            okay = false; break;
        }
        copied += got;
        if (app->poll) {
            t5_app_input_t input = {0};
            (void)app->poll(&input, 1);
        }
    }
    storage->stream_close(source);
    if (!usb_volume->file_close(usb_volume->context, target, okay && copied == source_size)) okay = false;
    if (!okay) usb_error("Copy to USB failed");
    return okay && copied == source_size;
}

static bool copy_usb_to_sd(const char *source, const char *destination_vfs) {
    if (!storage_write_stream_available() || !refresh_usb()) return false;
    uint64_t source_size = 0;
    risc_storage_file_t input = usb_volume->file_open_read(usb_volume->context, source, &source_size);
    if (!input || source_size > SIZE_MAX) return false;
    t5_storage_stream_t output = storage->write_stream_open(destination_vfs);
    if (!output) {
        (void)usb_volume->file_close(usb_volume->context, input, true);
        copy_text(status_text, sizeof(status_text), "Could not create SD file");
        return false;
    }
    size_t copied = 0;
    bool okay = true;
    const size_t expected = (size_t)source_size;
    while (copied < expected) {
        size_t request = expected - copied;
        if (request > sizeof(copy_buffer)) request = sizeof(copy_buffer);
        size_t got = usb_volume->file_read(usb_volume->context, input, copy_buffer, request);
        if (!got || storage->write_stream_write(output, copy_buffer, got) != got) {
            okay = false; break;
        }
        copied += got;
        if (app->poll) {
            t5_app_input_t event = {0};
            (void)app->poll(&event, 1);
        }
    }
    if (!usb_volume->file_close(usb_volume->context, input, true)) okay = false;
    if (okay && copied == expected) {
        if (!storage->write_stream_commit(output)) okay = false;
    } else storage->write_stream_abort(output);
    if (!okay) copy_text(status_text, sizeof(status_text), "Copy to SD failed");
    return okay && copied == expected;
}

static bool copy_selected(void) {
    if (!entry_count || selected_index < 0 || selected_index >= (int32_t)entry_count ||
        entries[selected_index].is_directory) return false;
    const bool source_usb = using_usb();
    if (!source_usb && !refresh_usb()) {
        usb_error("No USB storage connected");
        return false;
    }
    char destination_directory[PATH_CAP];
    if (!choose_destination(!source_usb, destination_directory, sizeof(destination_directory))) {
        if (!status_text[0]) copy_text(status_text, sizeof(status_text), "Copy cancelled");
        return false;
    }
    char destination[PATH_CAP];
    join_relative_path(destination_directory, entries[selected_index].name,
                       destination, sizeof(destination));
    if (source_usb) {
        char destination_vfs[PATH_CAP + 4];
        make_sd_vfs_dir(destination, destination_vfs, sizeof(destination_vfs));
        if (storage->exists && storage->exists(destination_vfs)) {
            copy_text(status_text, sizeof(status_text), "Destination already exists on SD");
            return false;
        }
        char source_path[PATH_CAP];
        make_usb_path(entries[selected_index].name, source_path, sizeof(source_path));
        snprintf(status_text, sizeof(status_text), "Copying %.96s to SD...", entries[selected_index].name);
        browser->render(base_path, status_text, ui_entries, entry_count, selected_index);
        if (!copy_usb_to_sd(source_path, destination_vfs)) return false;
        snprintf(status_text, sizeof(status_text), "Copied %.96s to SD", entries[selected_index].name);
        return true;
    }
    uint64_t existing_size = 0; bool existing_dir = false;
    if (usb_volume->stat(usb_volume->context, destination, &existing_size, &existing_dir)) {
        copy_text(status_text, sizeof(status_text), "Destination already exists on USB");
        return false;
    }
    char source_vfs[PATH_CAP + 4];
    make_vfs_path(entries[selected_index].name, source_vfs, sizeof(source_vfs));
    snprintf(status_text, sizeof(status_text), "Copying %.96s to USB...", entries[selected_index].name);
    browser->render(base_path, status_text, ui_entries, entry_count, selected_index);
    if (!copy_sd_to_usb(source_vfs, destination)) return false;
    snprintf(status_text, sizeof(status_text), "Copied %.96s to USB", entries[selected_index].name);
    return true;
}

#define MAX_OPEN_HANDLERS 8u
static int32_t choose_handler(const char *vfs_path, t5_file_handler_t *handlers, uint32_t *count_out) {
    t5_ui_list_row_t rows[MAX_OPEN_HANDLERS];
    char subtitles[MAX_OPEN_HANDLERS][40];
    uint32_t count = file_open->handler_count(vfs_path);
    if (count > MAX_OPEN_HANDLERS) count = MAX_OPEN_HANDLERS;
    if (count_out) *count_out = count;
    if (!count) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        memset(&handlers[i], 0, sizeof(handlers[i]));
        if (!file_open->handler_get(vfs_path, i, &handlers[i])) return -1;
        snprintf(subtitles[i], sizeof(subtitles[i]), "%s",
                 handlers[i].kind == T5_FILE_HANDLER_SYSTEM_READER ? "System" : "App");
        rows[i] = (t5_ui_list_row_t){handlers[i].display_name, subtitles[i], handlers[i].app_id, 0};
    }
    if (count == 1) return 0;
    return run_menu("Open with", "Choose an app for this file", rows, count);
}

static bool open_with_handler(const char *name, const t5_file_handler_t *handler) {
    char vfs_path[PATH_CAP + 4];
    char storage_path[PATH_CAP];
    make_vfs_path(name, vfs_path, sizeof(vfs_path));
    make_storage_path(name, storage_path, sizeof(storage_path));
    if (handler->kind == T5_FILE_HANDLER_SYSTEM_READER) {
        clear_session();
        if (browser->open_document(storage_path)) return true;
        copy_text(status_text, sizeof(status_text), "System reader could not open file");
        return false;
    }
    pending_delete_path[0] = 0;
    pending_delete_usb = false;
    save_session();
    if (file_open->open_request(vfs_path, handler->app_id, HANDOFF_COOKIE)) return true;
    clear_session();
    snprintf(status_text, sizeof(status_text), "Failed to open with %.80s", handler->display_name);
    return false;
}

static bool open_selected(void) {
    if (!entry_count || selected_index < 0 || selected_index >= (int32_t)entry_count) return false;
    browser_entry_t *entry = &entries[selected_index];
    if (entry->is_directory) {
        if (entry->usb_root) copy_text(base_path, sizeof(base_path), USB_ROOT);
        else if (strcmp(base_path, "/") == 0) {
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
    if (using_usb()) {
        copy_text(status_text, sizeof(status_text), "Copy this file to SD before opening it");
        return false;
    }
    if (ends_with_ci(entry->name, ".elf")) {
        char vfs_path[PATH_CAP + 4];
        make_vfs_path(entry->name, vfs_path, sizeof(vfs_path));
        pending_delete_path[0] = 0;
        pending_delete_usb = false;
        save_session();
        if (browser->launch_elf_request(vfs_path, HANDOFF_COOKIE)) return true;
        clear_session();
        copy_text(status_text, sizeof(status_text), "Native app failed");
        return false;
    }
    char vfs_path[PATH_CAP + 4];
    make_vfs_path(entry->name, vfs_path, sizeof(vfs_path));
    t5_file_handler_t handlers[MAX_OPEN_HANDLERS];
    uint32_t handler_count = 0;
    const int32_t choice = choose_handler(vfs_path, handlers, &handler_count);
    if (!handler_count) {
        copy_text(status_text, sizeof(status_text), "No registered app for this file type");
        return false;
    }
    if (choice < 0 || choice >= (int32_t)handler_count) {
        copy_text(status_text, sizeof(status_text), "Open cancelled");
        return false;
    }
    return open_with_handler(entry->name, &handlers[choice]);
}

static bool request_delete(void) {
    if (!entry_count || selected_index < 0 || selected_index >= (int32_t)entry_count ||
        entries[selected_index].is_directory) return false;
    pending_delete_usb = using_usb();
    if (pending_delete_usb) make_usb_path(entries[selected_index].name, pending_delete_path, sizeof(pending_delete_path));
    else make_storage_path(entries[selected_index].name, pending_delete_path, sizeof(pending_delete_path));
    save_session();
    if (browser->confirm_delete_request(entries[selected_index].name, HANDOFF_COOKIE)) return true;
    clear_session();
    return false;
}

static bool file_actions(void) {
    if (!entry_count || selected_index < 0 || selected_index >= (int32_t)entry_count ||
        entries[selected_index].is_directory) return false;
    const bool source_usb = using_usb();
    const bool can_copy = source_usb || refresh_usb();
    if (!can_copy) return request_delete();
    t5_ui_list_row_t rows[2] = {
        {source_usb ? "Copy to SD" : "Copy to USB", "Choose a destination folder", "", 0},
        {"Delete", "Remove this file", "", 0},
    };
    const int32_t action = run_menu(entries[selected_index].name, "File actions", rows, 2);
    if (action == 0) {
        char selected[T5_APP_DIRENT_NAME_MAX];
        selected_name(selected, sizeof(selected));
        (void)copy_selected();
        load_files(selected);
        return false;
    }
    if (action == 1) return request_delete();
    return false;
}

static void consume_handoff_results(void) {
    bool confirmed = false;
    uint64_t cookie = 0;
    if (browser->confirm_delete_take_result(&confirmed, &cookie)) {
        const int32_t old_index = selected_index;
        if (confirmed && pending_delete_path[0]) {
            bool deleted = false;
            if (pending_delete_usb) {
                deleted = refresh_usb() && usb_volume->remove(usb_volume->context, pending_delete_path);
                if (!deleted) usb_error("Failed to delete USB file");
            } else {
                deleted = browser->delete_document(pending_delete_path);
                if (!deleted) copy_text(status_text, sizeof(status_text), "Failed to delete file");
            }
            if (deleted) status_text[0] = 0;
        }
        pending_delete_path[0] = 0;
        pending_delete_usb = false;
        load_files(NULL);
        if (entry_count == 0) selected_index = 0;
        else if (old_index >= (int32_t)entry_count) selected_index = (int32_t)entry_count - 1;
        else selected_index = old_index;
        clear_session();
    }
    int32_t open_error = 0;
    if (file_open->open_take_result(&open_error, &cookie)) {
        char selected[T5_APP_DIRENT_NAME_MAX] = {0};
        selected_name(selected, sizeof(selected));
        if (open_error != 0) snprintf(status_text, sizeof(status_text), "File handler failed: %ld", (long)open_error);
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

static void acquire_usb(void) {
    providers = t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if (!providers || providers->api_version != T5_PROVIDER_CAPABILITY_API_VERSION ||
        providers->struct_size < sizeof(*providers) || !providers->acquire || !providers->release) return;
    const void *interface = NULL;
    if (!providers->acquire("storage.volume", RISC_STORAGE_VOLUME_API_V1, &usb_lease, &interface)) return;
    usb_volume = (const risc_storage_volume_api_v1 *)interface;
    if (!usb_api_valid(usb_volume)) {
        (void)providers->release(usb_lease);
        usb_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
        usb_volume = NULL;
    }
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    browser = t5_file_browser_get_api(T5_FILE_BROWSER_API_VERSION);
    file_open = t5_file_open_get_api(T5_FILE_OPEN_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !storage || !system_ui || !browser || !file_open || !ui ||
        !app->dir_open || !app->dir_next || !app->dir_close ||
        !app->set_back_exits_app || !storage->read_file || !storage->write_file_atomic || !storage->remove_file ||
        !system_ui->navigate_home || !browser->show_hidden_files || !browser->render || !browser->poll_event ||
        !browser->page_items || !browser->confirm_delete_request || !browser->confirm_delete_take_result ||
        !browser->delete_document || !browser->open_document || !browser->launch_elf_request ||
        !browser->launch_elf_take_result || file_open->api_version != T5_FILE_OPEN_API_VERSION ||
        file_open->struct_size < sizeof(*file_open) || !file_open->handler_count || !file_open->handler_get ||
        !file_open->open_request || !file_open->open_take_result || ui->api_version != T5_UI_API_VERSION ||
        !ui->render_list || !ui->poll_event || !ui->hit_test || !ui->next_index || !ui->previous_index) return;

    app->set_back_exits_app(false);
    status_text[0] = 0;
    pending_delete_path[0] = 0;
    pending_delete_usb = false;
    usb_volume = NULL;
    usb_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
    entry_count = 0;
    selected_index = 0;
    copy_text(base_path, sizeof(base_path), "/");
    acquire_usb();

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
                if (file_actions()) { app->set_back_exits_app(true); return; }
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
