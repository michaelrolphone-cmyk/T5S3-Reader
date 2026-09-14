#include "T5AppApi.h"
#include "T5KOReaderApi.h"
#include "T5NetworkApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIFI_COOKIE 0x4B4F524541555448ULL

static const t5_app_api_v1 *app;
static const t5_koreader_api_v1 *koreader;
static const t5_network_api_v1 *network;
static const t5_system_ui_api_v1 *system_ui;
static const t5_ui_api_v1 *ui;
static char status_text[160];
static bool finished;

static const char *auth_message(const t5_koreader_auth_result_t *result) {
    if (result && result->message[0]) return result->message;
    if (!result) return "Authentication failed";
    switch (result->status) {
        case T5_KOREADER_AUTH_OK: return "Authentication successful";
        case T5_KOREADER_AUTH_NO_CREDENTIALS: return "KOReader credentials are not configured";
        case T5_KOREADER_AUTH_NETWORK_ERROR: return "Network error";
        case T5_KOREADER_AUTH_FAILED: return "Authentication failed";
        case T5_KOREADER_AUTH_SERVER_ERROR: return "KOReader server error";
        case T5_KOREADER_AUTH_JSON_ERROR: return "Invalid server response";
        case T5_KOREADER_AUTH_NOT_FOUND: return "KOReader account not found";
        default: return "Authentication failed";
    }
}

static void render(void) {
    t5_ui_list_row_t row = {
        .title = finished ? "Result" : "Status",
        .subtitle = status_text,
        .value = finished ? "Done" : "Working",
        .flags = finished ? T5_UI_LIST_HIGHLIGHT_VALUE : 0,
    };
    const t5_ui_chrome_t chrome = {
        .title = "KOReader Authentication",
        .subtitle = "Test the configured sync credentials",
        .status = NULL,
        .back_label = "Back",
        .confirm_label = finished ? "OK" : "",
        .previous_label = "",
        .next_label = "",
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static bool ensure_wifi(void) {
    bool connected = false;
    bool cancelled = false;
    uint64_t cookie = 0;
    if (system_ui->wifi_take_result && system_ui->wifi_take_result(&connected, &cancelled, &cookie)) {
        if (cookie != WIFI_COOKIE || cancelled || !connected) {
            snprintf(status_text, sizeof(status_text), "Wi-Fi connection cancelled or failed");
            finished = true;
            render();
            return false;
        }
        return true;
    }
    if (network->wifi_connected()) return true;
    snprintf(status_text, sizeof(status_text), "Select a Wi-Fi network...");
    render();
    if (system_ui->wifi_request && system_ui->wifi_request(WIFI_COOKIE)) return false;
    snprintf(status_text, sizeof(status_text), "Wi-Fi selection unavailable");
    finished = true;
    render();
    return false;
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    koreader = t5_koreader_get_api(T5_KOREADER_API_VERSION);
    network = t5_network_get_api(T5_NETWORK_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !koreader || !network || !system_ui || !ui || !app->poll ||
        !koreader->authenticate || !koreader->end_auth_session || !network->wifi_connected ||
        !ui->render_list) return;

    finished = false;
    snprintf(status_text, sizeof(status_text), "Preparing authentication...");
    render();

    if (!ensure_wifi()) {
        if (!finished) return;
    } else {
        t5_koreader_auth_result_t result;
        memset(&result, 0, sizeof(result));
        snprintf(status_text, sizeof(status_text), "Authenticating...");
        render();
        const bool ok = koreader->authenticate(&result);
        snprintf(status_text, sizeof(status_text), "%s", ok ? auth_message(&result) : "Authentication service unavailable");
        finished = true;
        render();
    }

    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;
        if (finished && ((input.buttons & T5_APP_BUTTON_CONFIRM) || input.tapped)) break;
    }
    koreader->end_auth_session();
}
