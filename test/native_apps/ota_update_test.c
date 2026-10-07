#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5OtaApi.h"
#include "T5UiApi.h"

void app_main(void);

static t5_ota_result_t check_result;
static t5_ota_result_t install_result;
static bool newer;
static int exit_mode;
static t5_ui_event_type_t events[3];
static unsigned event_count;
static unsigned event_index;
static unsigned cases;
static int checks;
static int newer_queries;
static int version_queries;
static int check_error_renders;
static int install_error_renders;
static int complete_renders;
static int polls;
static int installs;
static int restarts;
static int progress_renders;
static int available_renders;
static int current_renders;
static size_t processed;
static size_t total;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    assert(polls <= 61);
    switch (exit_mode) {
        case 0: input->buttons = T5_APP_BUTTON_BACK; break;
        case 1: input->buttons = T5_APP_BUTTON_CONFIRM; break;
        case 2: input->tapped = true; break;
        case 3: input->exit_requested = true; break;
        case 4: return false;
        default: break;
    }
    return true;
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = app_poll,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

static t5_ota_result_t check_for_update(void) { ++checks; return check_result; }
static bool is_update_newer(void) { ++newer_queries; return newer; }
static bool latest_version(char *buffer, size_t size) {
    ++version_queries;
    assert(buffer && size > 0);
    strncpy(buffer, "1.2.0", size - 1);
    buffer[size - 1] = 0;
    return true;
}
static size_t processed_size(void) { return processed; }
static size_t total_size(void) { return total; }
static t5_ota_result_t install_update(t5_ota_progress_callback_t callback, void *ctx) {
    ++installs;
    total = 1000;
    processed = 500;
    callback(ctx);
    processed = 1000;
    callback(ctx);
    return install_result;
}
static void restart_after_update(void) { ++restarts; }

static const t5_ota_api_v1 ota_api = {
    .api_version = T5_OTA_API_VERSION,
    .struct_size = sizeof(t5_ota_api_v1),
    .check_for_update = check_for_update,
    .is_update_newer = is_update_newer,
    .latest_version = latest_version,
    .processed_size = processed_size,
    .total_size = total_size,
    .install_update = install_update,
    .restart_after_update = restart_after_update,
};

const t5_ota_api_v1 *t5_ota_get_api(uint32_t version) {
    return version == T5_OTA_API_VERSION ? &ota_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 1 && selected_index == 0);
    assert(strcmp(chrome->title, "Firmware Update") == 0);
    if (strcmp(rows[0].title, "New firmware available") == 0) {
        assert(strcmp(rows[0].value, "1.2.0") == 0);
        ++available_renders;
    } else if (strcmp(rows[0].title, "Installing update") == 0) {
        ++progress_renders;
    } else if (strcmp(rows[0].title, "No update available") == 0) {
        assert(strcmp(rows[0].value, "Up to date") == 0);
        ++current_renders;
    } else if (strcmp(rows[0].title, "Update check failed") == 0) {
        ++check_error_renders;
    } else if (strcmp(rows[0].title, "Update failed") == 0) {
        ++install_error_renders;
    } else if (strcmp(rows[0].title, "Update complete") == 0) {
        ++complete_renders;
    }
}

static int32_t hit_test(int16_t x, int16_t y) {
    (void)x;
    (void)y;
    return T5_UI_HIT_NONE;
}

// App rendering/service fixture. Real touch/edge dispatch is exercised by
// confirmation_input_test.cpp against the production UI functions.
static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    assert(event && wait_ms == 50);
    memset(event, 0, sizeof(*event));
    if (event_index == event_count) return false;
    assert(event_index < sizeof(events) / sizeof(events[0]));
    event->type = events[event_index++];
    return true;
}

static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
    .poll_event = poll_event,
};

const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

static void reset(void) {
    check_result = install_result = T5_OTA_OK;
    newer = false;
    exit_mode = 0;
    event_count = event_index = 0;
    checks = newer_queries = version_queries = 0;
    polls = installs = restarts = progress_renders = available_renders = current_renders = 0;
    check_error_renders = install_error_renders = complete_renders = 0;
    processed = total = 0;
    ++cases;
}

static void expect_no_install(void) {
    assert(checks == 1);
    assert(installs == 0 && restarts == 0 && progress_renders == 0 && complete_renders == 0);
}

int main(void) {
    // NO_UPDATE is authoritative even if a service retains stale newer metadata.
    // Each terminal input route must return cleanly without offering an install.
    for (int mode = 0; mode < 5; ++mode) {
        reset();
        check_result = T5_OTA_NO_UPDATE;
        newer = true;
        exit_mode = mode;
        app_main();
        assert(current_renders == 1 && check_error_renders == 0 && available_renders == 0);
        assert(newer_queries == 0 && version_queries == 0 && event_index == 0 && polls == 1);
        expect_no_install();
    }

    reset();
    app_main();
    assert(current_renders == 1 && check_error_renders == 0 && newer_queries == 1);
    expect_no_install();

    const t5_ota_result_t errors[] = {T5_OTA_HTTP_ERROR, T5_OTA_JSON_PARSE_ERROR,
        T5_OTA_UPDATE_OLDER_ERROR, T5_OTA_INTERNAL_UPDATE_ERROR, T5_OTA_OOM_ERROR,
        T5_OTA_UNAVAILABLE, (t5_ota_result_t)17};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        reset();
        check_result = errors[i];
        newer = true;
        app_main();
        assert(check_error_renders == 1 && current_renders == 0 && available_renders == 0);
        assert(newer_queries == 0 && version_queries == 0 && polls == 1);
        expect_no_install();
    }

    // Retry after failed/no-change invocations follows the normal consent path.
    // Body taps must still be ignored; Back, Exit and ended polling cancel.
    const t5_ui_event_type_t cancel[] = {T5_UI_EVENT_BACK, T5_UI_EVENT_EXIT};
    for (unsigned i = 0; i < 3; ++i) {
        reset();
        newer = true;
        events[0] = T5_UI_EVENT_TAP;
        events[1] = cancel[i % 2];
        event_count = i == 2 ? 1 : 2;
        app_main();
        assert(available_renders == 1 && current_renders == 0 && check_error_renders == 0);
        assert(newer_queries == 1 && version_queries == 1 && event_index == event_count);
        expect_no_install();
    }

    reset();
    newer = true;
    events[0] = T5_UI_EVENT_TAP;
    events[1] = T5_UI_EVENT_CONFIRM;
    event_count = 2;
    install_result = T5_OTA_HTTP_ERROR;
    app_main();
    assert(installs == 1 && install_error_renders == 1 && restarts == 0 && complete_renders == 0);
    assert(current_renders == 0 && check_error_renders == 0 && polls == 1);

    reset();
    newer = true;
    exit_mode = 5;
    events[0] = T5_UI_EVENT_TAP;
    events[1] = T5_UI_EVENT_CONFIRM;
    event_count = 2;
    app_main();
    assert(checks == 1 && newer_queries == 1 && version_queries == 1 && available_renders == 1);
    assert(installs == 1 && progress_renders == 3 && restarts == 1 && complete_renders == 1);
    assert(check_error_renders == 0 && install_error_renders == 0 && polls == 60);

    // Reopening after an installed/newer session cannot revive its old offer.
    reset();
    check_result = T5_OTA_NO_UPDATE;
    newer = true;
    app_main();
    assert(current_renders == 1 && available_renders == 0 && version_queries == 0);
    expect_no_install();
    printf("OTA update result/confirmation regression passed (%u cases)\n", cases);
    return 0;
}
