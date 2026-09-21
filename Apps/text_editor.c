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

#define ROOT "/sd/Documents"
#define PATH_MAXIMUM 160
#define MAX_DOCUMENTS 64
#define MAX_NAME 63

typedef enum {
    EDIT, OPEN, NAME_NEW, NAME_SAVE, DISCARD, OVERWRITE, FINISHED
} mode_t;
typedef enum { AFTER_OPEN, AFTER_NEW, AFTER_EXIT } pending_t;

static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_provider_capability_api_v1 *providers;
static const risc_usb_keyboard_api_v1 *keyboard;
static t5_provider_capability_lease_t keyboard_lease;
static uint64_t subscription;
static te_document doc;
static char read_buffer[TE_CAPACITY + 1];
static char path[PATH_MAXIMUM] = ROOT "/notes.md";
static char filename[MAX_NAME + 1] = "notes.md";
static char entry[MAX_DOCUMENTS][MAX_NAME + 1];
static char proposed[MAX_NAME + 1];
static char status[100];
static size_t entry_count, current_entry, name_length, top_row;
static mode_t mode;
static mode_t return_mode;
static pending_t pending;
static bool caps, connected, needs_draw;
static uint8_t last_buttons;

static void message(const char *value) {
    (void)snprintf(status, sizeof(status), "%s", value);
    needs_draw = true;
}
static bool is_document(const char *name) {
    const size_t length = name ? strlen(name) : 0;
    if (length < 5 || length > MAX_NAME || name[0] == '.' || strstr(name, "..")) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    const char *suffix = name + length - 4;
    const bool markdown = (suffix[0] == '.' &&
        (suffix[1] == 'm' || suffix[1] == 'M') &&
        (suffix[2] == 'd' || suffix[2] == 'D') && suffix[3] == 0);
    const bool text = length >= 5 &&
        (name[length - 4] == '.') &&
        (name[length - 3] == 't' || name[length - 3] == 'T') &&
        (name[length - 2] == 'x' || name[length - 2] == 'X') &&
        (name[length - 1] == 't' || name[length - 1] == 'T');
    return markdown || text;
}
static bool make_path(const char *name, char *out, size_t capacity) {
    if (!is_document(name)) return false;
    const int n = snprintf(out, capacity, ROOT "/%s", name);
    return n > 0 && (size_t)n < capacity;
}
static bool save(void) {
    if (!is_document(filename) || !storage->write_file_atomic(path, doc.text, doc.length)) {
        message("SAVE FAILED. Document remains unsaved.");
        return false;
    }
    doc.dirty = false;
    message("Saved to /Documents (atomic write).");
    return true;
}
static bool load(const char *name) {
    char target[PATH_MAXIMUM] = {0};
    size_t bytes = 0, read = 0;
    if (!make_path(name, target, sizeof(target)) ||
        !storage->read_file(target, 0, 0, &bytes) || bytes > TE_CAPACITY ||
        !storage->read_file(target, read_buffer, sizeof(read_buffer) - 1, &read) ||
        read != bytes || !te_import(&doc, read_buffer, read)) {
        message("Cannot open: missing, >16 KiB or non-ASCII text.");
        return false;
    }
    (void)snprintf(filename, sizeof(filename), "%s", name);
    (void)snprintf(path, sizeof(path), "%s", target);
    top_row = 0;
    mode = EDIT;
    message("Opened. Ctrl+S save; Ctrl+O open; Ctrl+Shift+S Save As.");
    return true;
}
static bool create_new(const char *name) {
    char target[PATH_MAXIMUM] = {0};
    if (!make_path(name, target, sizeof(target)) || storage->exists(target)) {
        message("Invalid name or file exists. Choose a new .md/.txt name.");
        return false;
    }
    te_reset(&doc);
    (void)snprintf(filename, sizeof(filename), "%s", name);
    (void)snprintf(path, sizeof(path), "%s", target);
    doc.dirty = true;  // A new empty document still needs its first save.
    top_row = 0;
    mode = EDIT;
    message("New document. Ctrl+S to create file.");
    return true;
}
static void listing(void) {
    entry_count = current_entry = 0;
    if (!app->dir_open(ROOT)) {
        message("No Documents directory. Press N to create one.");
        return;
    }
    t5_app_dirent_t item = {0};
    unsigned seen = 0;
    while (seen++ < 128 && entry_count < MAX_DOCUMENTS && app->dir_next(&item)) {
        if (item.is_directory || !is_document(item.name)) continue;
        (void)snprintf(entry[entry_count++], MAX_NAME + 1, "%s", item.name);
        if ((seen & 15u) == 0u) {
            t5_app_input_t ignored = {0};
            (void)app->poll(&ignored, 0);  // Real scheduler/watchdog checkpoint.
        }
    }
    app->dir_close();
    message("Enter opens; Up/Down browse; N new; Esc returns.");
}
static void start_name(mode_t destination) {
    mode = destination;
    proposed[0] = 0;
    name_length = 0;
    message("Type a filename ending .md or .txt, then Enter.");
}
static void request_transition(pending_t action) {
    pending = action;
    if (doc.dirty) {
        mode = DISCARD;
        message("Unsaved changes: S save, D discard, Esc cancel.");
    } else {
        if (action == AFTER_EXIT) mode = FINISHED;
        else if (action == AFTER_NEW) start_name(NAME_NEW);
        else { mode = OPEN; listing(); }
    }
}
static void finish_transition(void) {
    doc.dirty = false;
    if (pending == AFTER_EXIT) mode = FINISHED;
    else if (pending == AFTER_NEW) start_name(NAME_NEW);
    else { mode = OPEN; listing(); }
}
static char ascii(uint8_t usage, bool shifted) {
    if (usage >= 4 && usage <= 29) {
        const bool upper = shifted != caps;
        return (char)((upper ? 'A' : 'a') + (usage - 4));
    }
    if (usage >= 30 && usage <= 39) {
        static const char plain[] = "1234567890";
        static const char upper[] = "!@#$%^&*()";
        return shifted ? upper[usage - 30] : plain[usage - 30];
    }
    switch (usage) {
        case 0x2c: return ' ';
        case 0x2d: return shifted ? '_' : '-';
        case 0x2e: return shifted ? '+' : '=';
        case 0x2f: return shifted ? '{' : '[';
        case 0x30: return shifted ? '}' : ']';
        case 0x31: return shifted ? '|' : '\\';
        case 0x33: return shifted ? ':' : ';';
        case 0x34: return shifted ? '"' : '\'';
        case 0x35: return shifted ? '~' : '`';
        case 0x36: return shifted ? '<' : ',';
        case 0x37: return shifted ? '>' : '.';
        case 0x38: return shifted ? '?' : '/';
        default: return 0;
    }
}
static void on_key(const risc_usb_keyboard_event_v1 *event) {
    if (event->kind == 1) { connected = true; message("Keyboard connected."); return; }
    if (event->kind == 2) { connected = false; caps = false; message("Keyboard disconnected. Reconnect to continue."); return; }
    if (event->kind != 3 || event->usage >= 0xe0) return;
    const uint8_t key = event->usage;
    const bool shift = (event->modifiers & 0x22u) != 0;
    const bool ctrl = (event->modifiers & 0x11u) != 0;
    const bool alt = (event->modifiers & 0x44u) != 0;
    if (key == 0x39) { caps = !caps; needs_draw = true; return; }
    if (key == 0x29) {
        if (mode == EDIT) request_transition(AFTER_EXIT);
        else if (mode == DISCARD || mode == OVERWRITE) { mode = return_mode == EDIT ? EDIT : OPEN; message("Cancelled."); }
        else { mode = EDIT; message("Back to editor."); }
        return;
    }
    if (mode == DISCARD) {
        if (key == 0x16) { if (save()) finish_transition(); }
        else if (key == 0x07) finish_transition();
        return;
    }
    if (mode == OVERWRITE) {
        if (key == 0x28) {
            if (storage->write_file_atomic(path, doc.text, doc.length)) {
                doc.dirty = false; mode = EDIT; message("Saved As (existing file replaced).");
            } else { mode = EDIT; message("Save As failed; edits retained."); }
        }
        return;
    }
    if (mode == NAME_NEW || mode == NAME_SAVE) {
        if (key == 0x2a && name_length) proposed[--name_length] = 0;
        else if (key == 0x28) {
            if (mode == NAME_NEW) (void)create_new(proposed);
            else {
                char target[PATH_MAXIMUM] = {0};
                if (!make_path(proposed, target, sizeof(target))) message("Filename must end with .md or .txt.");
                else if (strcmp(target, path) == 0) { mode = EDIT; (void)save(); }
                else if (storage->exists(target)) message("File exists: choose a different name.");
                else {
                    char old_path[PATH_MAXIMUM], old_name[MAX_NAME + 1];
                    (void)snprintf(old_path, sizeof(old_path), "%s", path);
                    (void)snprintf(old_name, sizeof(old_name), "%s", filename);
                    if (storage->write_file_atomic(target, doc.text, doc.length)) {
                        (void)snprintf(path, sizeof(path), "%s", target);
                        (void)snprintf(filename, sizeof(filename), "%s", proposed);
                        doc.dirty = false; mode = EDIT; message("Saved As successfully.");
                    } else {
                        (void)snprintf(path, sizeof(path), "%s", old_path);
                        (void)snprintf(filename, sizeof(filename), "%s", old_name);
                        message("Save As failed; original document retained.");
                    }
                }
            }
        } else if (!ctrl && !alt && name_length < MAX_NAME) {
            const char ch = ascii(key, shift);
            if (ch && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) {
                proposed[name_length++] = ch; proposed[name_length] = 0;
            }
        }
        needs_draw = true;
        return;
    }
    if (mode == OPEN) {
        if (key == 0x52 && current_entry) --current_entry;
        else if (key == 0x51 && current_entry + 1 < entry_count) ++current_entry;
        else if (key == 0x28 && entry_count) (void)load(entry[current_entry]);
        else if (key == 0x11) request_transition(AFTER_NEW); // N
        needs_draw = true;
        return;
    }
    if (mode != EDIT) return;
    if (ctrl) {
        switch (key) {
            case 0x16: if (shift) start_name(NAME_SAVE); else (void)save(); break; // S
            case 0x12: request_transition(AFTER_OPEN); break; // O
            case 0x11: request_transition(AFTER_NEW); break; // N
            case 0x04: doc.selected = true; doc.anchor = 0; doc.cursor = doc.length; break; // A
            case 0x06: te_copy(&doc); break; // C
            case 0x1b: te_cut(&doc); break; // X
            case 0x19: if (!te_paste(&doc)) message("Clipboard empty or document full."); break; // V
            default: break;
        }
    } else if (!alt) {
        switch (key) {
            case 0x28: if (!te_character(&doc, '\n')) message("Document full."); break;
            case 0x2b: if (!te_character(&doc, '\t')) message("Document full."); break;
            case 0x2a: te_backspace(&doc); break;
            case 0x4c: te_delete(&doc); break;
            case 0x50: te_move(&doc, doc.cursor ? doc.cursor - 1 : 0, shift); break;
            case 0x4f: te_move(&doc, doc.cursor + (doc.cursor < doc.length), shift); break;
            case 0x52: te_vertical(&doc, false, shift); break;
            case 0x51: te_vertical(&doc, true, shift); break;
            case 0x4a: te_move(&doc, te_line_start(&doc, doc.cursor), shift); break;
            case 0x4d: te_move(&doc, te_line_end(&doc, doc.cursor), shift); break;
            default: {
                const char ch = ascii(key, shift);
                if (ch && !te_character(&doc, ch)) message("Document full.");
                break;
            }
        }
    }
    needs_draw = true;
}

static size_t cursor_row(size_t columns, size_t *column) {
    size_t row = 0, col = 0;
    for (size_t i = 0; i < doc.cursor; ++i) {
        if (doc.text[i] == '\n') { ++row; col = 0; }
        else if (++col >= columns) { ++row; col = 0; }
    }
    *column = col;
    return row;
}
static void render(void) {
    if (!app) return;
    int32_t width = app->screen_width(), height = app->screen_height();
    if (width < 120 || height < 120) return;
    size_t columns = (size_t)(width - 16) / 10u;
    size_t rows = (size_t)(height - 90) / 18u;
    if (columns < 8) columns = 8;
    if (columns > 90) columns = 90;
    if (rows < 2) rows = 2;
    if (rows > 48) rows = 48;
    char heading[100];
    (void)snprintf(heading, sizeof(heading), "Text Editor  %s%s", filename, doc.dirty ? " *" : "");
    app->clear();
    app->draw_text(8, 6, heading);
    app->fill_rect(8, 30, width - 16, 1, true);
    if (mode == EDIT) {
        size_t cursor_column = 0;
        const size_t target_row = cursor_row(columns, &cursor_column);
        if (target_row < top_row) top_row = target_row;
        if (target_row >= top_row + rows) top_row = target_row - rows + 1;
        char line[96];
        size_t line_length = 0, row = 0;
        for (size_t i = 0; i <= doc.length; ++i) {
            const char ch = i == doc.length ? 0 : doc.text[i];
            if (ch == '\n' || ch == 0 || line_length == columns) {
                line[line_length] = 0;
                if (row >= top_row && row - top_row < rows)
                    app->draw_text(8, 40 + (int32_t)(row - top_row) * 18, line);
                ++row;
                line_length = 0;
                if (ch == '\n' || ch == 0) continue;
            }
            line[line_length++] = ch == '\t' ? ' ' : ch;
        }
        app->fill_rect(8 + (int32_t)cursor_column * 10,
                       57 + (int32_t)(target_row - top_row) * 18, 8, 2, true);
    } else if (mode == OPEN) {
        app->draw_text(8, 40, "Documents folder: Enter open / N new / Esc cancel");
        size_t first = current_entry >= rows ? current_entry - rows + 1 : 0;
        for (size_t i = first; i < entry_count && i - first < rows; ++i) {
            char line[96];
            (void)snprintf(line, sizeof(line), "%c %s", i == current_entry ? '>' : ' ', entry[i]);
            app->draw_text(8, 64 + (int32_t)(i - first) * 18, line);
        }
        if (!entry_count) app->draw_text(8, 64, "No .md/.txt files. Press N to create.");
    } else if (mode == NAME_NEW || mode == NAME_SAVE) {
        app->draw_text(8, 44, mode == NAME_NEW ? "New document name:" : "Save As filename:");
        app->draw_text(8, 72, proposed);
        app->fill_rect(8 + (int32_t)name_length * 10, 89, 8, 2, true);
    } else if (mode == DISCARD) {
        app->draw_text(8, 55, "Unsaved changes: S save / D discard / Esc cancel");
    } else if (mode == OVERWRITE) {
        app->draw_text(8, 55, "Enter overwrite / Esc cancel");
    }
    app->fill_rect(8, height - 46, width - 16, 1, true);
    app->draw_text(8, height - 40, status);
    app->draw_text(8, height - 20,
        keyboard ? (connected ? "USB keyboard | Ctrl+S Save | Ctrl+O Open | Esc Exit" :
                              "USB keyboard | connect keyboard; Back exits") :
                   "Press device Confirm to enable USB keyboard; Back exits");
    app->present(false);
    needs_draw = false;
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    providers = t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if (!app || app->abi_version != T5_APP_ABI_VERSION ||
        app->struct_size < offsetof(t5_app_api_v1, set_back_exits_app) + sizeof(app->set_back_exits_app) ||
        !app->poll || !app->present || !app->draw_text || !app->clear ||
        !app->screen_width || !app->screen_height || !app->fill_rect ||
        !app->dir_open || !app->dir_next || !app->dir_close || !app->millis ||
        !storage || storage->api_version != T5_STORAGE_API_VERSION ||
        storage->struct_size < offsetof(t5_storage_api_v1, remove_file) + sizeof(storage->remove_file) ||
        !storage->exists || !storage->read_file || !storage->write_file_atomic ||
        !providers || providers->api_version != T5_PROVIDER_CAPABILITY_API_VERSION ||
        providers->struct_size < sizeof(*providers) || !providers->acquire || !providers->release) return;
    app->set_back_exits_app(false);
    te_reset(&doc);
    caps = connected = false;
    mode = EDIT;
    message("Press Confirm to enable USB keyboard. Ctrl+N creates a file.");
    uint32_t last_draw = 0;
    for (;;) {
        t5_app_input_t input = {0};
        if (!app->poll(&input, 25) || input.exit_requested) break;
        const uint8_t buttons = (uint8_t)input.buttons;
        const uint8_t pressed = buttons & (uint8_t)~last_buttons;
        last_buttons = buttons;
        if (pressed & T5_APP_BUTTON_BACK) request_transition(AFTER_EXIT);
        if ((pressed & T5_APP_BUTTON_CONFIRM) && !keyboard) {
            const void *interface = 0;
            if (!providers->acquire("usb.hid.keyboard", RISC_USB_KEYBOARD_API_V1,
                                    &keyboard_lease, &interface))
                message("Keyboard provider unavailable; install HID driver stack.");
            else {
                const risc_usb_keyboard_api_v1 *candidate =
                    (const risc_usb_keyboard_api_v1*)interface;
                if (candidate->api_version != RISC_USB_KEYBOARD_API_V1 ||
                    candidate->struct_size < sizeof(*candidate) || !candidate->subscribe ||
                    !candidate->unsubscribe || !candidate->poll || !candidate->next ||
                    !candidate->snapshot) {
                    (void)providers->release(keyboard_lease);
                    keyboard_lease = 0;
                    message("Installed keyboard provider has incompatible ABI.");
                } else {
                    keyboard = candidate;
                    subscription = keyboard->subscribe(keyboard->context, 0);
                    if (!subscription) {
                        keyboard = 0;
                        (void)providers->release(keyboard_lease);
                        keyboard_lease = 0;
                        message("Keyboard subscription exhausted.");
                    } else message("Listening for USB boot keyboard input.");
                }
            }
        }
        if (keyboard && subscription) {
            if (!keyboard->poll(keyboard->context, 4)) {
                message("HID poll error; reconnect keyboard or retry app.");
            } else {
                for (unsigned i = 0; i < 32; ++i) {
                    risc_usb_keyboard_event_v1 event = {0};
                    const int32_t rc = keyboard->next(keyboard->context, subscription, &event);
                    if (rc == 0) break;
                    if (rc < 0 || event.kind == 5) {
                        risc_usb_keyboard_state_v1 states[4] = {{0}};
                        size_t count = 4;
                        connected = keyboard->snapshot(keyboard->context, states, &count) && count != 0;
                        caps = false; // Resynchronize rather than invent lost key transitions.
                        message("Keyboard queue gap; state resynchronized.");
                        break;
                    }
                    on_key(&event);
                }
            }
        }
        if (mode == FINISHED) break;
        const uint32_t now = app->millis();
        if (needs_draw && (last_draw == 0 || (uint32_t)(now - last_draw) >= 120)) {
            render(); last_draw = now;
        }
    }
    if (keyboard && subscription) (void)keyboard->unsubscribe(keyboard->context, subscription);
    subscription = 0;
    keyboard = 0;
    if (keyboard_lease) (void)providers->release(keyboard_lease);
    keyboard_lease = 0;
    app->set_back_exits_app(true);
}
