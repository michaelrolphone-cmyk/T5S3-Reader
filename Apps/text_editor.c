#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "T5ProviderCapabilityApi.h"
#include "RiscUsbHidV1.h"
#include "text_editor_core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DOCUMENTS "/sd/Documents"
#define NAME_LIMIT 63u
#define FILE_LIMIT 64u
#define FOOTER_HEIGHT 144
#define KEYBOARD_BUTTON_HEIGHT 44

typedef enum { EDITING, FILE_PICKER, NEW_NAME, SAVE_NAME, UNSAVED, DONE } editor_mode_t;
typedef enum { DO_OPEN, DO_NEW, DO_EXIT } after_t;
static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_provider_capability_api_v1 *providers;
static const risc_usb_keyboard_api_v1 *keyboard;
static t5_provider_capability_lease_t grant;
static uint64_t subscription;
static te_document document;
static char scratch[TE_CAPACITY + 1];
static char path[160], filename[NAME_LIMIT + 1], proposed[NAME_LIMIT + 1];
static char files[FILE_LIMIT][NAME_LIMIT + 1];
static char status[96];
static char keyboard_error[160];
static size_t file_count, selected_file, proposed_size, first_row;
static editor_mode_t mode;
static after_t after;
static bool resume_after_save, caps, plugged, redraw;
static uint8_t old_buttons;

static void report(const char *text) {
    (void)snprintf(status, sizeof(status), "%s", text);
    redraw = true;
}

// Documents is intentionally the only editable directory in this initial app.
// Reject traversal, hidden names, separators and all but .txt/.md extensions.
static bool valid_name(const char *name) {
    const size_t len = name ? strlen(name) : 0;
    if (len < 4 || len > NAME_LIMIT || name[0] == '.') return false;
    for (size_t i = 0; i < len; ++i) {
        const char c = name[i];
        if (c == '.' && i + 1 < len && name[i + 1] == '.') return false;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    const bool md = len >= 4 && name[len - 3] == '.' &&
        (name[len - 2] == 'm' || name[len - 2] == 'M') &&
        (name[len - 1] == 'd' || name[len - 1] == 'D');
    const bool txt = len >= 5 && name[len - 4] == '.' &&
        (name[len - 3] == 't' || name[len - 3] == 'T') &&
        (name[len - 2] == 'x' || name[len - 2] == 'X') &&
        (name[len - 1] == 't' || name[len - 1] == 'T');
    return md || txt;
}
static bool filename_path(const char *name, char *result, size_t cap) {
    if (!valid_name(name)) return false;
    const int n = snprintf(result, cap, DOCUMENTS "/%s", name);
    return n > 0 && (size_t)n < cap;
}
static void begin_name(editor_mode_t target) {
    mode = target;
    proposed_size = 0; proposed[0] = 0;
    report("Enter new .txt/.md filename; Enter confirms; Esc cancels.");
}
static bool save_current(void) {
    if (!path[0] || !valid_name(filename)) {
        begin_name(SAVE_NAME);
        return false;
    }
    if (!storage->write_file_atomic(path, document.text, document.length)) {
        report("Save FAILED. Edits remain in memory.");
        return false;
    }
    document.dirty = false;
    report("Saved via atomic file replacement.");
    return true;
}
static bool load_file(const char *name) {
    char target[160] = {0};
    size_t size = 0, count = 0;
    if (!filename_path(name, target, sizeof(target)) ||
        !storage->read_file(target, NULL, 0, &size) || size > TE_CAPACITY ||
        !storage->read_file(target, scratch, TE_CAPACITY, &count) || count != size ||
        !te_import(&document, scratch, count)) {
        report("Open rejected: missing, >16 KiB, binary or non-ASCII.");
        return false;
    }
    (void)snprintf(path, sizeof(path), "%s", target);
    (void)snprintf(filename, sizeof(filename), "%s", name);
    first_row = 0; mode = EDITING;
    report("Opened. Ctrl+S save / Ctrl+O open / Ctrl+N new.");
    return true;
}
static bool make_new(const char *name) {
    char target[160] = {0};
    if (!filename_path(name, target, sizeof(target)) || storage->exists(target)) {
        report("Name invalid or already exists. Choose another name.");
        return false;
    }
    te_reset(&document);
    (void)snprintf(filename, sizeof(filename), "%s", name);
    (void)snprintf(path, sizeof(path), "%s", target);
    document.dirty = true;
    first_row = 0; mode = EDITING;
    report("New file. Ctrl+S saves it to Documents.");
    return true;
}
static void list_files(void) {
    file_count = selected_file = 0;
    if (!app->dir_open(DOCUMENTS)) {
        report("Documents is empty. Press N to create a file.");
        return;
    }
    t5_app_dirent_t item = {0};
    for (unsigned seen = 0; seen < 128 && file_count < FILE_LIMIT; ++seen) {
        if (!app->dir_next(&item)) break;
        if (!item.is_directory && valid_name(item.name))
            (void)snprintf(files[file_count++], NAME_LIMIT + 1, "%.*s", (int)NAME_LIMIT, item.name);
        if ((seen & 15u) == 15u) {
            t5_app_input_t unused = {0};
            (void)app->poll(&unused, 0); // Cooperative checkpoint during scan.
        }
    }
    app->dir_close();
    report("Up/Down select; Enter open; N new; Esc cancel.");
}
static void continue_after(void) {
    resume_after_save = false;
    if (after == DO_EXIT) mode = DONE;
    else if (after == DO_NEW) begin_name(NEW_NAME);
    else { mode = FILE_PICKER; list_files(); }
}
static void transition(after_t action) {
    after = action;
    if (document.dirty) {
        mode = UNSAVED;
        report("Unsaved changes: S save / D discard / Esc cancel.");
    } else continue_after();
}
static char translate(uint8_t key, bool shift) {
    if (key >= 4 && key <= 29)
        return (char)(((shift != caps) ? 'A' : 'a') + key - 4);
    if (key >= 30 && key <= 39) {
        static const char normal[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        return shift ? shifted[key - 30] : normal[key - 30];
    }
    switch (key) {
        case 0x2c: return ' ';
        case 0x2d: return shift ? '_' : '-';
        case 0x2e: return shift ? '+' : '=';
        case 0x2f: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        default: return 0;
    }
}
static void key_press(uint8_t key, uint8_t modifiers) {
    const bool shift = (modifiers & 0x22u) != 0;
    const bool ctrl = (modifiers & 0x11u) != 0;
    const bool alt = (modifiers & 0x44u) != 0;
    if (key == 0x39) { caps = !caps; redraw = true; return; }
    if (key == 0x29) {
        if (mode == EDITING) transition(DO_EXIT);
        else if (mode == SAVE_NAME && resume_after_save) {
            mode = UNSAVED; report("Unsaved changes: S save / D discard / Esc cancel.");
        } else { mode = EDITING; resume_after_save = false; report("Cancelled."); }
        return;
    }
    if (mode == UNSAVED) {
        if (key == 0x16) {
            if (save_current()) continue_after();
            else if (mode == SAVE_NAME) resume_after_save = true;
        } else if (key == 0x07) { document.dirty = false; continue_after(); }
        return;
    }
    if (mode == NEW_NAME || mode == SAVE_NAME) {
        if (key == 0x2a) {
            if (proposed_size) proposed[--proposed_size] = 0;
        } else if (key == 0x28) {
            if (mode == NEW_NAME) (void)make_new(proposed);
            else {
                char target[160] = {0};
                if (!filename_path(proposed, target, sizeof(target)))
                    report("Invalid filename: use .md or .txt and no slashes.");
                else if (strcmp(target, path) == 0) {
                    mode = EDITING;
                    if (save_current() && resume_after_save) continue_after();
                } else if (storage->exists(target)) {
                    report("Save As won't overwrite. Choose a new filename.");
                } else if (!storage->write_file_atomic(target, document.text, document.length)) {
                    report("Save As failed. Original and edits retained.");
                } else {
                    (void)snprintf(path, sizeof(path), "%s", target);
                    (void)snprintf(filename, sizeof(filename), "%s", proposed);
                    document.dirty = false;
                    mode = EDITING;
                    report("Saved As new file.");
                    if (resume_after_save) continue_after();
                }
            }
        } else if (!ctrl && !alt && proposed_size < NAME_LIMIT) {
            const char ch = translate(key, shift);
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.') {
                proposed[proposed_size++] = ch;
                proposed[proposed_size] = 0;
            }
        }
        redraw = true;
        return;
    }
    if (mode == FILE_PICKER) {
        if (key == 0x52 && selected_file) --selected_file;
        else if (key == 0x51 && selected_file + 1 < file_count) ++selected_file;
        else if (key == 0x28 && file_count) (void)load_file(files[selected_file]);
        else if (key == 0x11) transition(DO_NEW);
        redraw = true;
        return;
    }
    if (mode != EDITING) return;
    if (ctrl) {
        switch (key) {
            case 0x16: if (shift) begin_name(SAVE_NAME); else (void)save_current(); break;
            case 0x12: transition(DO_OPEN); break;
            case 0x11: transition(DO_NEW); break;
            case 0x04: document.selected = true; document.anchor = 0;
                       document.cursor = document.length; break;
            case 0x06: te_copy(&document); break;
            case 0x1b: te_cut(&document); break;
            case 0x19: if (!te_paste(&document)) report("Clipboard empty or file full."); break;
            default: break;
        }
    } else if (!alt) {
        switch (key) {
            case 0x28: if (!te_character(&document, '\n')) report("File full."); break;
            case 0x2b: if (!te_character(&document, '\t')) report("File full."); break;
            case 0x2a: te_backspace(&document); break;
            case 0x4c: te_delete(&document); break;
            case 0x50: te_move(&document, document.cursor ? document.cursor - 1 : 0, shift); break;
            case 0x4f: te_move(&document, document.cursor + (document.cursor < document.length), shift); break;
            case 0x52: te_vertical(&document, false, shift); break;
            case 0x51: te_vertical(&document, true, shift); break;
            case 0x4a: te_move(&document, te_line_start(&document, document.cursor), shift); break;
            case 0x4d: te_move(&document, te_line_end(&document, document.cursor), shift); break;
            default: {
                const char ch = translate(key, shift);
                if (ch && !te_character(&document, ch)) report("File full.");
                break;
            }
        }
    }
    redraw = true;
}

static size_t cursor_visual_row(size_t columns, size_t *column) {
    size_t row = 0, col = 0;
    for (size_t i = 0; i < document.cursor; ++i) {
        if (document.text[i] == '\n') { ++row; col = 0; }
        else if (++col == columns) { ++row; col = 0; }
    }
    *column = col;
    return row;
}
static void paint(void) {
    const int32_t width = app->screen_width(), height = app->screen_height();
    if (width < 120 || height < 120) return;
    size_t columns = (size_t)(width - 16) / 10u;
    size_t rows = height > FOOTER_HEIGHT + 82 ?
        (size_t)(height - FOOTER_HEIGHT - 82) / 18u : 1;
    if (columns < 8) columns = 8;
    if (columns > 90) columns = 90;
    if (rows < 1) rows = 1;
    if (rows > 48) rows = 48;
    char heading[100];
    (void)snprintf(heading, sizeof(heading), "Text Editor  %s%s",
                   filename[0] ? filename : "Untitled", document.dirty ? " *" : "");
    app->clear();
    app->draw_text(8, 6, heading);
    app->fill_rect(8, 30, width - 16, 1, true);
    if (keyboard_error[0] && !keyboard) {
        app->draw_label(8, 48, width - 16, "Keyboard activation failed");
        // Short rows keep the complete diagnostic readable in both orientations.
        for (size_t offset = 0, row = 0; keyboard_error[offset] && row < 5; ++row) {
            char line[33] = {0};
            size_t count = strlen(keyboard_error + offset);
            if (count > 32) count = 32;
            memcpy(line, keyboard_error + offset, count);
            app->draw_label(8, 88 + (int32_t)row * 32, width - 16, line);
            offset += count;
        }
    } else if (mode == EDITING) {
        size_t column = 0;
        const size_t cursor_row = cursor_visual_row(columns, &column);
        if (cursor_row < first_row) first_row = cursor_row;
        if (cursor_row >= first_row + rows) first_row = cursor_row - rows + 1;
        char line[96];
        size_t line_size = 0, row = 0;
        for (size_t i = 0; i <= document.length; ++i) {
            const char ch = i == document.length ? 0 : document.text[i];
            if (ch == '\n' || ch == 0 || line_size == columns) {
                line[line_size] = 0;
                if (row >= first_row && row - first_row < rows)
                    app->draw_text(8, 40 + (int32_t)(row - first_row) * 18, line);
                ++row;
                line_size = 0;
                if (ch == '\n' || ch == 0) continue;
            }
            line[line_size++] = ch == '\t' ? ' ' : ch;
        }
        app->fill_rect(8 + (int32_t)column * 10,
                       57 + (int32_t)(cursor_row - first_row) * 18, 8, 2, true);
    } else if (mode == FILE_PICKER) {
        app->draw_text(8, 42, "Documents: Enter open / N new / Esc cancel");
        const size_t first = selected_file >= rows ? selected_file - rows + 1 : 0;
        for (size_t i = first; i < file_count && i - first < rows; ++i) {
            char line[96];
            (void)snprintf(line, sizeof(line), "%c %s", selected_file == i ? '>' : ' ', files[i]);
            app->draw_text(8, 64 + (int32_t)(i - first) * 18, line);
        }
        if (!file_count) app->draw_text(8, 64, "No .md/.txt files. N creates one.");
    } else if (mode == NEW_NAME || mode == SAVE_NAME) {
        app->draw_text(8, 44, mode == NEW_NAME ? "New filename:" : "Save As filename:");
        app->draw_text(8, 72, proposed);
        app->fill_rect(8 + (int32_t)proposed_size * 10, 89, 8, 2, true);
    } else if (mode == UNSAVED) {
        app->draw_text(8, 55, "Unsaved: S save / D discard / Esc cancel");
    }
    const int32_t footer = height - FOOTER_HEIGHT;
    app->fill_rect(8, footer, width - 16, 1, true);
    app->draw_label(8, footer + 8, width - 16, status);
    app->draw_label(8, footer + 40, width - 16,
        keyboard ? (plugged ? "USB keyboard connected" : "Connect USB keyboard") :
                   "Tap below to enable USB keyboard");
    if (!keyboard) {
        const int32_t top = height - KEYBOARD_BUTTON_HEIGHT - 12;
        app->fill_rect(8, top, width - 16, 1, true);
        app->fill_rect(8, top + KEYBOARD_BUTTON_HEIGHT - 1, width - 16, 1, true);
        app->fill_rect(8, top, 1, KEYBOARD_BUTTON_HEIGHT, true);
        app->fill_rect(width - 9, top, 1, KEYBOARD_BUTTON_HEIGHT, true);
        app->draw_label(16, top + 8, width - 32, "Enable keyboard");
    } else {
        app->draw_label(8, height - 44, width - 16, "Ctrl+S Save / Ctrl+O Open");
    }
    app->present(false);
    redraw = false;
}
void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    providers = t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if (!app || app->abi_version != T5_APP_ABI_VERSION ||
        app->struct_size < offsetof(t5_app_api_v1, draw_label) + sizeof(app->draw_label) ||
        !app->poll || !app->present || !app->draw_text || !app->clear ||
        !app->screen_width || !app->screen_height || !app->fill_rect ||
        !app->draw_label || !app->set_back_exits_app ||
        !app->dir_open || !app->dir_next || !app->dir_close || !app->millis ||
        !storage || storage->api_version != T5_STORAGE_API_VERSION ||
        storage->struct_size < offsetof(t5_storage_api_v1, remove_file) + sizeof(storage->remove_file) ||
        !storage->exists || !storage->read_file || !storage->write_file_atomic ||
        !providers || providers->api_version != T5_PROVIDER_CAPABILITY_API_VERSION ||
        providers->struct_size < offsetof(t5_provider_capability_api_v1, release) + sizeof(providers->release) || !providers->acquire || !providers->release) return;
    app->set_back_exits_app(false);
    te_reset(&document);
    path[0] = filename[0] = 0; // Never implicitly save an empty buffer over an existing file.
    caps = plugged = false;
    old_buttons = 0;
    keyboard_error[0] = 0;
    mode = EDITING;
    report("Untitled. Ctrl+N new / Ctrl+O open.");
    uint32_t last_paint = 0;
    for (;;) {
        t5_app_input_t input = {0};
        if (!app->poll(&input, 25) || input.exit_requested) break;
        const uint8_t buttons = (uint8_t)input.buttons;
        const uint8_t pressed = buttons & (uint8_t)~old_buttons;
        old_buttons = buttons;
        if (pressed & T5_APP_BUTTON_BACK) transition(DO_EXIT);
        const int32_t button_top = app->screen_height() - KEYBOARD_BUTTON_HEIGHT - 12;
        const bool enable_tapped = input.tapped &&
            input.touch_x >= 8 && input.touch_x < app->screen_width() - 8 &&
            input.touch_y >= button_top &&
            input.touch_y < button_top + KEYBOARD_BUTTON_HEIGHT;
        if (mode != DONE && !keyboard &&
            ((pressed & T5_APP_BUTTON_CONFIRM) || enable_tapped)) {
            const void *interface = NULL;
            if (!providers->acquire("usb.hid.keyboard", RISC_USB_KEYBOARD_API_V1,
                                    &grant, &interface)) {
                (void)snprintf(keyboard_error, sizeof(keyboard_error),
                    "Firmware has no detailed capability diagnostics. Update firmware to see failure cause.");
                if (providers->struct_size >= offsetof(t5_provider_capability_api_v1, last_error) +
                        sizeof(providers->last_error) && providers->last_error) {
                    if (!providers->last_error(keyboard_error, sizeof(keyboard_error)))
                        (void)snprintf(keyboard_error, sizeof(keyboard_error), "Capability request failed without a diagnostic.");
                }
                report("See error above. Tap Enable keyboard to retry.");
            } else {
                keyboard_error[0] = 0;
                const risc_usb_keyboard_api_v1 *api = (const risc_usb_keyboard_api_v1*)interface;
                if (!api || api->api_version != RISC_USB_KEYBOARD_API_V1 ||
                    api->struct_size < sizeof(*api) || !api->subscribe ||
                    !api->unsubscribe || !api->poll || !api->next || !api->snapshot) {
                    (void)providers->release(grant);
                    grant = 0;
                    report("Keyboard capability ABI mismatch.");
                } else {
                    keyboard = api;
                    subscription = keyboard->subscribe(keyboard->context, 0);
                    if (!subscription) {
                        keyboard = NULL;
                        (void)providers->release(grant);
                        grant = 0;
                        report("Keyboard has no free subscription.");
                    } else report("USB keyboard active: connect a boot keyboard.");
                }
            }
        }
        if (keyboard && subscription) {
            if (!keyboard->poll(keyboard->context, 4)) {
                // Poll failure does not authorize fallback to firmware USB.
                report("HID poll failed; reconnect device or restart app.");
            } else {
                for (unsigned i = 0; i < 32; ++i) {
                    risc_usb_keyboard_event_v1 event = {0};
                    const int32_t result = keyboard->next(keyboard->context, subscription, &event);
                    if (!result) break;
                    if (result < 0 || event.kind == 5) {
                        risc_usb_keyboard_state_v1 states[4] = {{0}};
                        size_t count = 4;
                        plugged = keyboard->snapshot(keyboard->context, states, &count) && count != 0;
                        caps = false;
                        report("HID queue gap: state resynchronized.");
                        break;
                    }
                    if (event.kind == 1) { plugged = true; report("Keyboard connected."); }
                    else if (event.kind == 2) {
                        plugged = caps = false;
                        report("Keyboard disconnected; reconnect to continue.");
                    } else if (event.kind == 3 && event.usage < 0xe0)
                        key_press(event.usage, event.modifiers);
                }
            }
        }
        if (mode == DONE) break;
        const uint32_t now = app->millis();
        if (redraw && (!last_paint || (uint32_t)(now - last_paint) >= 120)) {
            paint(); last_paint = now;
        }
    }
    if (keyboard && subscription) (void)keyboard->unsubscribe(keyboard->context, subscription);
    subscription = 0; keyboard = NULL;
    if (grant) (void)providers->release(grant);
    grant = 0;
    app->set_back_exits_app(true);
}
