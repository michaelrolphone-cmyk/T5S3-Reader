#include "T5AppApi.h"
#include "T5KOReaderApi.h"
#include "T5NetworkApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ITEM_COUNT 5u
#define COOKIE_USERNAME 0x4B4F55534552ULL
#define COOKIE_PASSWORD 0x4B4F50415353ULL
#define COOKIE_SERVER 0x4B4F55524C20ULL
#define COOKIE_AUTH_WIFI 0x4B4F57494649ULL

static const t5_app_api_v1 *app;
static const t5_koreader_api_v1 *koreader;
static const t5_network_api_v1 *network;
static const t5_system_ui_api_v1 *system_ui;
static const t5_ui_api_v1 *ui;

static t5_koreader_settings_t settings;
static int32_t selected_index;
static char username_value[T5_KOREADER_USERNAME_MAX];
static char password_value[16];
static char server_value[T5_KOREADER_SERVER_URL_MAX];
static char match_value[16];
static char auth_value[48];
static char status_text[160];
static bool auth_screen;
static bool auth_success;
static bool auth_session_active;

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

static bool refresh_settings(void) {
    memset(&settings, 0, sizeof(settings));
    return koreader->read_settings(&settings);
}

static void render_settings(void) {
    copy_text(username_value, sizeof(username_value), settings.username[0] ? settings.username : "Not set");
    copy_text(password_value, sizeof(password_value), settings.password[0] ? "******" : "Not set");
    copy_text(server_value, sizeof(server_value), settings.server_url[0] ? settings.server_url : "Default");
    copy_text(match_value, sizeof(match_value),
              settings.match_method == T5_KOREADER_MATCH_BINARY ? "Binary" : "Filename");
    copy_text(auth_value, sizeof(auth_value), settings.has_credentials ? "" : "[Set credentials first]");

    const t5_ui_chrome_t chrome = {
        .title = "KOReader Sync",
        .subtitle = "Account and sync settings",
        .status = status_text,
        .back_label = "Back",
        .confirm_label = "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    const t5_ui_list_row_t rows[ITEM_COUNT] = {
        {.title = "Username", .subtitle = NULL, .value = username_value, .flags = 0},
        {.title = "Password", .subtitle = NULL, .value = password_value, .flags = 0},
        {.title = "Sync Server URL", .subtitle = NULL, .value = server_value, .flags = 0},
        {.title = "Document Matching", .subtitle = NULL, .value = match_value, .flags = 0},
        {.title = "Authenticate", .subtitle = NULL, .value = auth_value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    ui->render_list(&chrome, rows, ITEM_COUNT, selected_index);
}

static void render_auth(void) {
    const char *value = auth_success ? "Success" : "Failed";
    const t5_ui_chrome_t chrome = {
        .title = "KOReader Authentication",
        .subtitle = auth_success ? "Authentication successful" : "Authentication failed",
        .status = status_text,
        .back_label = "Back",
        .confirm_label = "OK",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t row = {
        .title = "Status", .subtitle = NULL, .value = value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static void close_auth_screen(void) {
    if (auth_session_active) {
        koreader->end_auth_session();
        auth_session_active = false;
    }
    auth_screen = false;
    copy_text(status_text, sizeof(status_text), "");
    refresh_settings();
    selected_index = 4;
    render_settings();
}

static void authenticate_now(void) {
    t5_koreader_auth_result_t result;
    memset(&result, 0, sizeof(result));
    auth_session_active = true;
    auth_screen = true;
    if (!koreader->authenticate(&result)) {
        auth_success = false;
        copy_text(status_text, sizeof(status_text), "Authentication service unavailable");
    } else if (result.status == T5_KOREADER_AUTH_OK) {
        auth_success = true;
        copy_text(status_text, sizeof(status_text), "Authentication successful | Sync ready");
    } else {
        auth_success = false;
        if (result.http_status > 0) {
            snprintf(status_text, sizeof(status_text), "%s | HTTP %ld", result.message, (long)result.http_status);
        } else {
            copy_text(status_text, sizeof(status_text), result.message[0] ? result.message : "Authentication failed");
        }
    }
    render_auth();
}

static void apply_keyboard_result(void) {
    char text[T5_KOREADER_SERVER_URL_MAX];
    bool cancelled = false;
    uint64_t cookie = 0;
    if (!system_ui->keyboard_take_result(text, sizeof(text), &cancelled, &cookie)) return;
    if (cancelled) return;

    bool saved = false;
    if (cookie == COOKIE_USERNAME) {
        saved = koreader->set_username(text);
        selected_index = 0;
    } else if (cookie == COOKIE_PASSWORD) {
        saved = koreader->set_password(text);
        selected_index = 1;
    } else if (cookie == COOKIE_SERVER) {
        if (strcmp(text, "https://") == 0 || strcmp(text, "http://") == 0) text[0] = 0;
        saved = koreader->set_server_url(text);
        selected_index = 2;
    }
    copy_text(status_text, sizeof(status_text), saved ? "Saved" : "Could not save setting");
}

static void apply_wifi_result(void) {
    bool connected = false;
    bool cancelled = false;
    uint64_t cookie = 0;
    if (!system_ui->wifi_take_result(&connected, &cancelled, &cookie)) return;
    if (cookie != COOKIE_AUTH_WIFI) return;
    selected_index = 4;
    if (cancelled || !connected) {
        auth_screen = true;
        auth_success = false;
        copy_text(status_text, sizeof(status_text), "Wi-Fi connection failed");
        render_auth();
        return;
    }
    authenticate_now();
}

static bool request_keyboard(int32_t index) {
    if (index == 0) {
        return system_ui->keyboard_request("KOReader Username", settings.username, 64, T5_SYSTEM_KEYBOARD_TEXT,
                                           COOKIE_USERNAME);
    }
    if (index == 1) {
        return system_ui->keyboard_request("KOReader Password", settings.password, 64, T5_SYSTEM_KEYBOARD_PASSWORD,
                                           COOKIE_PASSWORD);
    }
    if (index == 2) {
        const char *initial = settings.server_url[0] ? settings.server_url : "https://";
        return system_ui->keyboard_request("Sync Server URL", initial, 128, T5_SYSTEM_KEYBOARD_URL, COOKIE_SERVER);
    }
    return false;
}

static bool activate_selected(void) {
    if (selected_index >= 0 && selected_index <= 2) return request_keyboard(selected_index);
    if (selected_index == 3) {
        const uint8_t next = settings.match_method == T5_KOREADER_MATCH_FILENAME ? T5_KOREADER_MATCH_BINARY
                                                                                 : T5_KOREADER_MATCH_FILENAME;
        if (koreader->set_match_method(next)) {
            refresh_settings();
            copy_text(status_text, sizeof(status_text), "Saved");
        } else {
            copy_text(status_text, sizeof(status_text), "Could not save setting");
        }
        render_settings();
        return false;
    }
    if (selected_index == 4) {
        if (!settings.has_credentials) {
            copy_text(status_text, sizeof(status_text), "Set username and password first");
            render_settings();
            return false;
        }
        if (network->wifi_connected()) {
            authenticate_now();
            return false;
        }
        if (!system_ui->wifi_request(COOKIE_AUTH_WIFI)) {
            copy_text(status_text, sizeof(status_text), "Could not open Wi-Fi selection");
            render_settings();
            return false;
        }
        return true;
    }
    return false;
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    koreader = t5_koreader_get_api(T5_KOREADER_API_VERSION);
    network = t5_network_get_api(T5_NETWORK_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !koreader || !network || !system_ui || !ui || !app->poll || !koreader->read_settings ||
        !koreader->set_username || !koreader->set_password || !koreader->set_server_url ||
        !koreader->set_match_method || !koreader->authenticate || !koreader->end_auth_session ||
        !network->wifi_connected || !system_ui->keyboard_request || !system_ui->keyboard_take_result ||
        !system_ui->wifi_request || !system_ui->wifi_take_result || !ui->render_list || !ui->hit_test ||
        !ui->next_index || !ui->previous_index) return;

    selected_index = 0;
    auth_screen = false;
    auth_success = false;
    auth_session_active = false;
    status_text[0] = 0;

    apply_keyboard_result();
    if (!refresh_settings()) return;
    apply_wifi_result();
    if (!auth_screen) render_settings();

    bool handoff_pending = false;
    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested) break;

        if (auth_screen) {
            if ((input.buttons & (T5_APP_BUTTON_BACK | T5_APP_BUTTON_CONFIRM)) || input.tapped) {
                close_auth_screen();
            }
            continue;
        }

        if (input.buttons & T5_APP_BUTTON_BACK) break;
        if ((input.buttons & T5_APP_BUTTON_UP) || (input.buttons & T5_APP_BUTTON_LEFT)) {
            selected_index = ui->previous_index(selected_index, ITEM_COUNT);
            render_settings();
            continue;
        }
        if ((input.buttons & T5_APP_BUTTON_DOWN) || (input.buttons & T5_APP_BUTTON_RIGHT)) {
            selected_index = ui->next_index(selected_index, ITEM_COUNT);
            render_settings();
            continue;
        }
        if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit >= 0 && hit < (int32_t)ITEM_COUNT) {
                selected_index = hit;
                handoff_pending = activate_selected();
                if (handoff_pending) break;
            }
            continue;
        }
        if (input.buttons & T5_APP_BUTTON_CONFIRM) {
            handoff_pending = activate_selected();
            if (handoff_pending) break;
        }
    }

    if (!handoff_pending && auth_session_active) koreader->end_auth_session();
}
