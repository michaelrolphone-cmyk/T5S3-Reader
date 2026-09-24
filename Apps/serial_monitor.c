/* Keep the normal Serial Monitor interface intact while surfacing capability
 * and asynchronous USB enumeration errors in its scrollable terminal. No USB
 * hardware or vendor protocol is implemented by the application. */
#include "T5SerialPortApi.h"
#include "T5UiApi.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* An absent or temporarily unavailable device must not trap the application
 * in repeated, synchronous ELF activation with a non-interactive spinner.
 * The app's existing event loop owns reconnection and app exit; this adapter
 * simply limits failed provider retries and keeps the terminal on screen.
 * No synthetic serial lease, stream, or hardware capability is returned. */
#define FAILED_ACQUIRE_SKIP_CYCLES 4u
static const t5_serial_port_api_v1* providerSerialApi;
static t5_serial_port_api_v1 displaySerialApi;
static const t5_ui_api_v1* providerUiApi;
static t5_ui_api_v1 displayUiApi;
static int32_t previousRuntimeError;
static char previousFailure[384];
static unsigned failedAcquireSkips;
static bool providerUnavailable;
static bool terminalRendered;
static t5_serial_result_t displayAcquire(const t5_serial_port_request_t* request,
                                        t5_serial_port_lease_t* lease,
                                        t5_stream_t* rx, t5_stream_t* tx);
static t5_serial_result_t displayReadStatus(t5_serial_port_lease_t lease,
                                           t5_serial_port_state_t* state);
static const t5_serial_port_api_v1* displaySerialGetApi(uint32_t version);
static const t5_ui_api_v1* displayUiGetApi(uint32_t version);
static void displayRenderList(const t5_ui_chrome_t* chrome,
                              const t5_ui_list_row_t* rows,
                              uint32_t row_count, int32_t selected_index);
static void displayRenderText(const t5_ui_chrome_t* chrome, const char* text,
                              int32_t scroll_from_bottom,
                              t5_ui_text_view_result_t* result);

#define t5_serial_port_get_api displaySerialGetApi
#define t5_ui_get_api displayUiGetApi
#include "serial_monitor_implementation.inc"
#undef t5_ui_get_api
#undef t5_serial_port_get_api

static void displayRenderList(const t5_ui_chrome_t* chrome,
                              const t5_ui_list_row_t* rows,
                              uint32_t row_count, int32_t selected_index) {
    /* Retain the last interactive terminal during a retry after a failed
     * acquisition. Back/Home must not appear disabled by another spinner. */
    if (terminalRendered && providerUnavailable && chrome && chrome->subtitle &&
        strcmp(chrome->subtitle, "Starting serial.port") == 0 &&
        row_count == 1u && rows && rows[0].title &&
        strcmp(rows[0].title, "Reconnecting serial session") == 0) return;
    providerUiApi->render_list(chrome, rows, row_count, selected_index);
}

static void displayRenderText(const t5_ui_chrome_t* chrome, const char* text,
                              int32_t scroll_from_bottom,
                              t5_ui_text_view_result_t* result) {
    terminalRendered = true;
    providerUiApi->render_text_view(chrome, text, scroll_from_bottom, result);
}

static const t5_ui_api_v1* displayUiGetApi(uint32_t version) {
    providerUiApi = t5_ui_get_api(version);
    if (!providerUiApi) return NULL;
    displayUiApi = *providerUiApi;
    if (providerUiApi->render_list) displayUiApi.render_list = displayRenderList;
    if (providerUiApi->render_text_view) displayUiApi.render_text_view = displayRenderText;
    return &displayUiApi;
}

static t5_serial_result_t displayAcquire(const t5_serial_port_request_t* request,
                                        t5_serial_port_lease_t* lease,
                                        t5_stream_t* rx, t5_stream_t* tx) {
    char failure[384];
    if (lease) *lease = 0;
    if (rx) *rx = 0;
    if (tx) *tx = 0;
    if (providerUnavailable && failedAcquireSkips) {
        --failedAcquireSkips;
        return T5_SERIAL_IO;
    }
    const t5_serial_result_t result = providerSerialApi->acquire(request, lease, rx, tx);
    if (result == T5_SERIAL_OK && lease && *lease && rx && *rx && tx && *tx) {
        previousFailure[0] = 0;
        previousRuntimeError = 0;
        failedAcquireSkips = 0;
        providerUnavailable = false;
        return result;
    }
    providerUnavailable = true;
    failedAcquireSkips = FAILED_ACQUIRE_SKIP_CYCLES;
    t5_serial_diagnostic_t details = {0};
    const size_t required = offsetof(t5_serial_port_api_v1, last_diagnostic) +
                            sizeof(providerSerialApi->last_diagnostic);
    if (providerSerialApi->struct_size >= required &&
        providerSerialApi->last_diagnostic &&
        providerSerialApi->last_diagnostic(&details) && details.detail[0]) {
        details.detail[sizeof(details.detail) - 1u] = 0;
        snprintf(failure, sizeof(failure), "%s", details.detail);
    } else {
        snprintf(failure, sizeof(failure),
                 "serial.port acquire rc=%ld; detailed provider diagnostics unavailable",
                 (long)result);
    }
    if (strcmp(previousFailure, failure) != 0) {
        append_notice(failure);
        snprintf(previousFailure, sizeof(previousFailure), "%s", failure);
    }
    scroll_from_bottom = 0;
    return result;
}

static t5_serial_result_t displayReadStatus(t5_serial_port_lease_t lease,
                                           t5_serial_port_state_t* state) {
    const t5_serial_result_t result = providerSerialApi->read_status(lease, state);
    if (result != T5_SERIAL_OK || !state) return result;
    const int32_t code = state->status == T5_SERIAL_STATUS_ERROR ? state->last_error : 0;
    if (code && code != previousRuntimeError) {
        char notice[192];
        if (code == -1240)
            snprintf(notice, sizeof(notice),
                     "USB device enumerated, but no serial class ELF bound. Check CH34x/CDC (including ST-LINK VCP)/CP210x/FTDI driver installation (error %ld)",
                     (long)code);
        else if (code == -1241)
            snprintf(notice, sizeof(notice),
                     "USB device attached, but its configuration descriptor is unavailable (error %ld)",
                     (long)code);
        else
            snprintf(notice, sizeof(notice), "USB serial provider error %ld", (long)code);
        append_notice(notice);
        scroll_from_bottom = 0;
    }
    previousRuntimeError = code;
    return result;
}

static const t5_serial_port_api_v1* displaySerialGetApi(uint32_t version) {
    providerSerialApi = t5_serial_port_get_api(version);
    if (!providerSerialApi) return NULL;
    previousRuntimeError = 0;
    previousFailure[0] = 0;
    failedAcquireSkips = 0;
    providerUnavailable = false;
    terminalRendered = false;
    displaySerialApi = *providerSerialApi;
    displaySerialApi.acquire = displayAcquire;
    displaySerialApi.read_status = displayReadStatus;
    return &displaySerialApi;
}
