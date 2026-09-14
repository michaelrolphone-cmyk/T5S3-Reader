#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5UiApi.h"
#include "T5WebServerApi.h"

void app_main(void);

static bool started;
static bool stopped;
static int services;
static int renders;
static int events;
static uint32_t fake_millis;
static t5_web_server_state_t server_state;

static uint32_t app_millis(void) {
    fake_millis += 1000u;
    return fake_millis;
}
static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .millis = app_millis,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

static bool web_supported(void) { return true; }
static void web_default_config(t5_web_server_config_t *config) {
    assert(config);
    memset(config, 0, sizeof(*config));
    strcpy(config->ssid, "Manifold");
    strcpy(config->hostname, "manifold");
    strcpy(config->document_root, "/html");
    config->channel = 1;
    config->max_connections = 4;
}
static bool web_start(const t5_web_server_config_t *config) {
    assert(config);
    assert(strcmp(config->ssid, "Manifold") == 0);
    assert(strcmp(config->hostname, "manifold") == 0);
    assert(strcmp(config->document_root, "/html") == 0);
    assert(config->password[0] == '\0');
    started = true;
    memset(&server_state, 0, sizeof(server_state));
    server_state.status = T5_WEB_SERVER_STATUS_RUNNING;
    server_state.channel = 1;
    server_state.dns_active = 1;
    server_state.mdns_active = 1;
    strcpy(server_state.ip, "192.168.4.1");
    strcpy(server_state.ssid, "Manifold");
    strcpy(server_state.hostname, "manifold.local");
    strcpy(server_state.document_root, "/html");
    return true;
}
static void web_stop(void) { stopped = true; }
static bool web_service(void) {
    ++services;
    if (services >= 2) {
        server_state.clients = 1;
        server_state.requests = 3;
        server_state.files_served = 2;
        server_state.bytes_served = 4096;
    }
    return true;
}
static bool web_read_state(t5_web_server_state_t *state) {
    assert(state);
    *state = server_state;
    return true;
}
static const t5_web_server_api_v1 web_api = {
    .api_version = T5_WEB_SERVER_API_VERSION,
    .struct_size = sizeof(t5_web_server_api_v1),
    .supported = web_supported,
    .default_config = web_default_config,
    .start = web_start,
    .stop = web_stop,
    .service = web_service,
    .read_state = web_read_state,
};
const t5_web_server_api_v1 *t5_web_server_get_api(uint32_t version) {
    return version == T5_WEB_SERVER_API_VERSION ? &web_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 7 && selected_index == 0);
    assert(strcmp(chrome->title, "Web Server") == 0);
    assert(strcmp(chrome->subtitle, "Manifold captive portal") == 0);
    assert(strcmp(rows[1].value, "Manifold") == 0);
    assert(strcmp(rows[2].value, "http://manifold.local/") == 0);
    assert(strcmp(rows[3].value, "192.168.4.1") == 0);
    assert(strcmp(rows[6].value, "/html") == 0);
    if (renders == 0) {
        assert(strcmp(rows[4].value, "0") == 0);
        assert(strcmp(rows[5].value, "0") == 0);
    } else {
        assert(strcmp(rows[4].value, "1") == 0);
        assert(strcmp(rows[5].value, "3") == 0);
    }
    ++renders;
}
static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    assert(event && wait_ms == 20);
    memset(event, 0, sizeof(*event));
    ++events;
    if (events >= 3) event->type = T5_UI_EVENT_BACK;
    return true;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .poll_event = poll_event,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    app_main();
    assert(started);
    assert(stopped);
    assert(services == 3);
    assert(renders == 2);
    assert(events == 3);
    return 0;
}
