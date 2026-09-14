#include "T5AppApi.h"
#include "T5UiApi.h"
#include "T5WebServerApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static const t5_app_api_v1 *core;
static const t5_ui_api_v1 *ui;
static const t5_web_server_api_v1 *web;

static char status_value[32];
static char ssid_value[48];
static char portal_value[96];
static char ip_value[32];
static char clients_value[24];
static char requests_value[32];
static char root_value[96];
static char footer[96];

static void copy_text(char *dest, uint32_t capacity, const char *source) {
    if (!dest || capacity == 0) return;
    if (!source) source = "";
    uint32_t i = 0;
    while (i + 1u < capacity && source[i]) {
        dest[i] = source[i];
        ++i;
    }
    dest[i] = '\0';
}

static const char *status_name(uint8_t status) {
    switch (status) {
        case T5_WEB_SERVER_STATUS_RUNNING: return "Running";
        case T5_WEB_SERVER_STATUS_OFF: return "Stopped";
        case T5_WEB_SERVER_STATUS_ERROR: return "Error";
        default: return "Unsupported";
    }
}

static void render_state(const t5_web_server_state_t *state) {
    copy_text(status_value, sizeof(status_value), status_name(state->status));
    copy_text(ssid_value, sizeof(ssid_value), state->ssid[0] ? state->ssid : "Manifold");
    snprintf(portal_value, sizeof(portal_value), "http://%s/", state->hostname[0] ? state->hostname : "manifold.local");
    copy_text(ip_value, sizeof(ip_value), state->ip[0] ? state->ip : "--");
    snprintf(clients_value, sizeof(clients_value), "%u", (unsigned)state->clients);
    snprintf(requests_value, sizeof(requests_value), "%lu", (unsigned long)state->requests);
    copy_text(root_value, sizeof(root_value), state->document_root[0] ? state->document_root : "/html");

    if (state->status == T5_WEB_SERVER_STATUS_ERROR) {
        snprintf(footer, sizeof(footer), "Server error %ld", (long)state->last_error);
    } else if (state->status == T5_WEB_SERVER_STATUS_RUNNING) {
        copy_text(footer, sizeof(footer), "Connect to the AP and open manifold.local");
    } else {
        copy_text(footer, sizeof(footer), "Back exits and stops the access point");
    }

    const t5_ui_chrome_t chrome = {
        .title = "Web Server",
        .subtitle = "Manifold captive portal",
        .status = footer,
        .back_label = "Stop",
        .confirm_label = "",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t rows[] = {
        {.title = "Status", .subtitle = NULL, .value = status_value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "Wi-Fi", .subtitle = NULL, .value = ssid_value, .flags = 0},
        {.title = "Portal", .subtitle = NULL, .value = portal_value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "IP", .subtitle = NULL, .value = ip_value, .flags = 0},
        {.title = "Clients", .subtitle = NULL, .value = clients_value, .flags = 0},
        {.title = "Requests", .subtitle = NULL, .value = requests_value, .flags = 0},
        {.title = "SD root", .subtitle = NULL, .value = root_value, .flags = 0},
    };
    ui->render_list(&chrome, rows, (uint32_t)(sizeof(rows) / sizeof(rows[0])), 0);
}

void app_main(void) {
    core = t5_app_get_api(T5_APP_ABI_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    web = t5_web_server_get_api(T5_WEB_SERVER_API_VERSION);
    if (!core || !ui || !web || !core->millis || !ui->render_list || !ui->poll_event ||
        !web->default_config || !web->start || !web->stop || !web->service || !web->read_state) return;

    t5_web_server_config_t config = {0};
    t5_web_server_state_t state = {0};
    web->default_config(&config);
    (void)web->start(&config);
    (void)web->read_state(&state);
    render_state(&state);

    uint32_t last_render = core->millis();
    uint8_t last_clients = state.clients;
    uint32_t last_requests = state.requests;

    for (;;) {
        (void)web->service();
        (void)web->read_state(&state);

        const uint32_t now = core->millis();
        if ((state.clients != last_clients || state.requests != last_requests) && now - last_render >= 2000u) {
            last_clients = state.clients;
            last_requests = state.requests;
            last_render = now;
            render_state(&state);
        }

        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) break;
        if (event.type == T5_UI_EVENT_EXIT || event.type == T5_UI_EVENT_BACK) break;
    }

    web->stop();
}
