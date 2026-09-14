#include "T5AppApi.h"
#include "T5OpdsApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COOKIE_PREFIX 0x4F50445300000000ULL
#define COOKIE_MASK   0xFFFFFFFF00000000ULL
#define LIST_MAX (T5_OPDS_MAX_SERVERS + 1u)
#define EDIT_ITEMS 5u

static const t5_app_api_v1 *app;
static const t5_opds_api_v1 *opds;
static const t5_system_ui_api_v1 *system_ui;
static const t5_ui_api_v1 *ui;

static t5_opds_server_t servers[T5_OPDS_MAX_SERVERS];
static t5_ui_list_row_t list_rows[LIST_MAX];
static char list_values[LIST_MAX][T5_OPDS_URL_MAX];
static uint32_t server_count;
static int32_t selected_index;
static bool editing;
static bool editor_is_new;
static uint32_t editor_index;
static t5_opds_server_t edit_server;
static char status_text[128];

static void copy_text(char *dst, size_t capacity, const char *src) {
    size_t i = 0;
    if (!dst || capacity == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < capacity) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

static uint64_t make_cookie(uint32_t index_code, uint8_t field) {
    return COOKIE_PREFIX | ((uint64_t)(index_code & 0xFFFFu) << 8) | field;
}

static bool parse_cookie(uint64_t cookie, uint32_t *index_code, uint8_t *field) {
    if ((cookie & COOKIE_MASK) != (COOKIE_PREFIX & COOKIE_MASK)) return false;
    if (index_code) *index_code = (uint32_t)((cookie >> 8) & 0xFFFFu);
    if (field) *field = (uint8_t)(cookie & 0xFFu);
    return true;
}

static void refresh_servers(void) {
    server_count = opds->count();
    if (server_count > T5_OPDS_MAX_SERVERS) server_count = T5_OPDS_MAX_SERVERS;
    for (uint32_t i = 0; i < server_count; ++i) {
        memset(&servers[i], 0, sizeof(servers[i]));
        opds->read(i, &servers[i]);
    }
}

static void render_list(void) {
    refresh_servers();
    memset(list_rows, 0, sizeof(list_rows));
    memset(list_values, 0, sizeof(list_values));
    const uint32_t count = server_count + (server_count < T5_OPDS_MAX_SERVERS ? 1u : 0u);
    if (count == 0) selected_index = 0;
    else if (selected_index < 0 || (uint32_t)selected_index >= count) selected_index = 0;

    for (uint32_t i = 0; i < server_count; ++i) {
        list_rows[i].title = servers[i].name[0] ? servers[i].name : servers[i].url;
        if (servers[i].name[0] && servers[i].url[0]) {
            copy_text(list_values[i], sizeof(list_values[i]), servers[i].url);
            list_rows[i].subtitle = list_values[i];
        }
    }
    if (count > server_count) {
        list_rows[server_count].title = "Add Server";
        list_rows[server_count].flags = T5_UI_LIST_HIGHLIGHT_VALUE;
    }

    const t5_ui_chrome_t chrome = {
        .title = "OPDS Servers",
        .subtitle = "Configure OPDS and Calibre catalogs",
        .status = status_text,
        .back_label = "Back",
        .confirm_label = "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, list_rows, count, selected_index);
}

static void render_editor(void) {
    char password_value[16];
    copy_text(password_value, sizeof(password_value), edit_server.password[0] ? "******" : "Not set");
    const char *name_value = edit_server.name[0] ? edit_server.name : "Not set";
    const char *url_value = edit_server.url[0] ? edit_server.url : "Not set";
    const char *user_value = edit_server.username[0] ? edit_server.username : "Not set";
    const uint32_t item_count = editor_is_new ? 4u : EDIT_ITEMS;
    if (selected_index < 0 || (uint32_t)selected_index >= item_count) selected_index = 0;

    const t5_ui_list_row_t rows[EDIT_ITEMS] = {
        {.title="Server Name", .value=name_value},
        {.title="Server URL", .value=url_value},
        {.title="Username", .value=user_value},
        {.title="Password", .value=password_value},
        {.title="Delete Server", .value="", .flags=T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    const t5_ui_chrome_t chrome = {
        .title = editor_is_new ? "Add Server" : "OPDS Server",
        .subtitle = "Calibre URL usually ends in /opds",
        .status = status_text,
        .back_label = "Back",
        .confirm_label = "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, item_count, selected_index);
}

static bool persist_edit(void) {
    if (editor_is_new) {
        uint32_t new_index = 0;
        if (!opds->add(&edit_server, &new_index)) return false;
        editor_is_new = false;
        editor_index = new_index;
        return true;
    }
    return opds->update(editor_index, &edit_server);
}

static bool request_field_keyboard(uint8_t field) {
    const char *title = "";
    const char *initial = "";
    size_t max_length = 63;
    uint8_t type = T5_SYSTEM_KEYBOARD_TEXT;
    switch (field) {
        case 0: title = "Server Name"; initial = edit_server.name; break;
        case 1: title = "OPDS Server URL"; initial = edit_server.url[0] ? edit_server.url : "https://";
                max_length = 127; type = T5_SYSTEM_KEYBOARD_URL; break;
        case 2: title = "Username"; initial = edit_server.username; break;
        case 3: title = "Password"; initial = edit_server.password; type = T5_SYSTEM_KEYBOARD_PASSWORD; break;
        default: return false;
    }
    const uint32_t index_code = editor_is_new ? 0u : editor_index + 1u;
    return system_ui->keyboard_request(title, initial, max_length, type, make_cookie(index_code, field));
}

static bool apply_keyboard_result(void) {
    char text[T5_OPDS_URL_MAX];
    bool cancelled = false;
    uint64_t cookie = 0;
    if (!system_ui->keyboard_take_result(text, sizeof(text), &cancelled, &cookie)) return false;
    uint32_t index_code = 0;
    uint8_t field = 0;
    if (!parse_cookie(cookie, &index_code, &field) || field > 3u) return false;

    memset(&edit_server, 0, sizeof(edit_server));
    editor_is_new = index_code == 0;
    editor_index = editor_is_new ? 0u : index_code - 1u;
    if (!editor_is_new && !opds->read(editor_index, &edit_server)) {
        copy_text(status_text, sizeof(status_text), "Server no longer exists");
        editing = false;
        return true;
    }
    editing = true;
    selected_index = field;
    if (cancelled) return true;

    if (field == 1u && (strcmp(text, "https://") == 0 || strcmp(text, "http://") == 0)) text[0] = 0;
    if (field == 0u) copy_text(edit_server.name, sizeof(edit_server.name), text);
    else if (field == 1u) copy_text(edit_server.url, sizeof(edit_server.url), text);
    else if (field == 2u) copy_text(edit_server.username, sizeof(edit_server.username), text);
    else copy_text(edit_server.password, sizeof(edit_server.password), text);

    copy_text(status_text, sizeof(status_text), persist_edit() ? "Saved" : "Could not save server");
    return true;
}

static bool activate_editor(void) {
    if (selected_index >= 0 && selected_index <= 3) return request_field_keyboard((uint8_t)selected_index);
    if (selected_index == 4 && !editor_is_new) {
        if (opds->remove(editor_index)) {
            editing = false;
            selected_index = 0;
            copy_text(status_text, sizeof(status_text), "Server deleted");
            render_list();
        } else {
            copy_text(status_text, sizeof(status_text), "Could not delete server");
            render_editor();
        }
    }
    return false;
}

static void open_selected(void) {
    refresh_servers();
    memset(&edit_server, 0, sizeof(edit_server));
    if (selected_index >= 0 && (uint32_t)selected_index < server_count) {
        editor_index = (uint32_t)selected_index;
        editor_is_new = false;
        opds->read(editor_index, &edit_server);
    } else {
        editor_index = 0;
        editor_is_new = true;
    }
    editing = true;
    selected_index = 0;
    status_text[0] = 0;
    render_editor();
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    opds = t5_opds_get_api(T5_OPDS_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !opds || !system_ui || !ui || !app->poll || !opds->count || !opds->read || !opds->add ||
        !opds->update || !opds->remove || !system_ui->keyboard_request || !system_ui->keyboard_take_result ||
        !ui->render_list || !ui->hit_test || !ui->next_index || !ui->previous_index) return;

    selected_index = 0;
    editing = false;
    status_text[0] = 0;
    apply_keyboard_result();
    if (editing) render_editor(); else render_list();

    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested) break;
        if (input.buttons & T5_APP_BUTTON_BACK) {
            if (editing) {
                editing = false;
                selected_index = 0;
                status_text[0] = 0;
                render_list();
                continue;
            }
            break;
        }

        const uint32_t count = editing ? (editor_is_new ? 4u : EDIT_ITEMS)
                                       : (server_count + (server_count < T5_OPDS_MAX_SERVERS ? 1u : 0u));
        if ((input.buttons & T5_APP_BUTTON_UP) || (input.buttons & T5_APP_BUTTON_LEFT)) {
            selected_index = ui->previous_index(selected_index, count);
            if (editing) render_editor(); else render_list();
            continue;
        }
        if ((input.buttons & T5_APP_BUTTON_DOWN) || (input.buttons & T5_APP_BUTTON_RIGHT)) {
            selected_index = ui->next_index(selected_index, count);
            if (editing) render_editor(); else render_list();
            continue;
        }
        if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit >= 0 && (uint32_t)hit < count) {
                selected_index = hit;
                if (editing) {
                    if (activate_editor()) break;
                } else open_selected();
            }
            continue;
        }
        if (input.buttons & T5_APP_BUTTON_CONFIRM) {
            if (editing) {
                if (activate_editor()) break;
            } else open_selected();
        }
    }
}
