#include <T5AppApi.h>
#include <T5DeviceApi.h>
#include <T5LocationApi.h>
#include <T5StreamApi.h>
#include <T5UiApi.h>
#include <RiscRteLocationRecords.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Run the actual diagnostic app against deterministic ABI fixtures. Polling
// continues while reads are paused; consent release denies an issued handle.
static uint32_t clock_ms, queued, peak, produced, received, polls;
static unsigned step, requests, releases, subscriptions, unsubscriptions, probes;
static int authorized;
static char verdict[96], count_text[24];
static bool app_poll(t5_app_input_t *input, uint32_t ms) {
    (void)ms;
    if (input) *input = (t5_app_input_t){0};
    return true;
}
static uint32_t app_millis(void) { return clock_ms; }
static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION, .struct_size = sizeof(t5_app_api_v1),
    .poll = app_poll, .millis = app_millis,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}
static void render(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                   uint32_t count, int32_t selected) {
    (void)selected;
    assert(chrome && rows && count == 7 && chrome->status);
    snprintf(verdict, sizeof(verdict), "%s", chrome->status);
    snprintf(count_text, sizeof(count_text), "%s", rows[1].value);
    if (queued == 4 && authorized) assert(strstr(rows[2].value, "4/4") != NULL);
}
static bool event(t5_ui_event_t *out, uint32_t ms) {
    (void)ms;
    assert(out);
    *out = (t5_ui_event_t){0};
    clock_ms += 250;
    ++step;
    if (step == 1 || step == 8) out->type = T5_UI_EVENT_CONFIRM;
    else if (step == 10) out->type = T5_UI_EVENT_NEXT;
    else if (step == 12) out->type = T5_UI_EVENT_BACK;
    assert(step <= 12);
    return true;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION, .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render, .poll_event = event,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}
static t5_device_result_t inventory(t5_device_info_t *out, uint32_t capacity,
                                    uint32_t *count) {
    assert(out && capacity >= 1 && count);
    *count = 1;
    out[0] = (t5_device_info_t){0};
    out[0].handle = 7;
    out[0].state = T5_DEVICE_AVAILABLE;
    out[0].transport = T5_DEVICE_TRANSPORT_UART;
    out[0].capability_count = 1;
    strcpy(out[0].provider, "gps-nmea");
    strcpy(out[0].capabilities[0], "location.position");
    return T5_DEVICE_OK;
}
static t5_device_result_t request(const char *name, t5_device_handle_t device,
                                  uint32_t rights, t5_device_lease_t *out) {
    assert(!strcmp(name, "location.position") && device == 7 &&
           rights == T5_DEVICE_RIGHT_READ && out);
    ++requests;
    authorized = 1;
    *out = 44;
    return T5_DEVICE_OK;
}
static t5_device_result_t release(t5_device_lease_t lease) {
    assert(lease == 44 && authorized);
    ++releases;
    authorized = 0;
    return T5_DEVICE_OK;
}
static const t5_device_api_v3 devices = {
    .v2 = {.v1 = {.api_version = T5_DEVICE_API_VERSION_3,
                   .struct_size = sizeof(t5_device_api_v3), .inventory = inventory},
           .release = release}, .request = request,
};
const t5_device_api_v1 *t5_device_get_api(uint32_t version) {
    return version == T5_DEVICE_API_VERSION_3 ? &devices.v2.v1 : NULL;
}
static t5_stream_result_t subscribe(t5_device_lease_t auth,
                                    t5_location_subscription_t *token, t5_stream_t *stream) {
    assert(auth == 44 && authorized && token && stream);
    ++subscriptions;
    *token = 55;
    *stream = 66;
    return T5_STREAM_OK;
}
static t5_stream_result_t poll(t5_device_lease_t auth) {
    assert(auth == 44 && authorized);
    ++polls;
    if (queued == 4) return T5_STREAM_AGAIN;
    ++queued;
    ++produced;
    if (queued > peak) peak = queued;
    return T5_STREAM_OK;
}
static t5_stream_result_t unsubscribe(t5_location_subscription_t token) {
    assert(token == 55);
    ++unsubscriptions;
    return T5_STREAM_OK;
}
static const t5_location_api_v1 location = {
    .api_version = T5_LOCATION_API_VERSION, .struct_size = sizeof(t5_location_api_v1),
    .subscribe = subscribe, .poll = poll, .unsubscribe = unsubscribe,
};
const t5_location_api_v1 *t5_location_get_api(uint32_t version) {
    return version == T5_LOCATION_API_VERSION ? &location : NULL;
}
static void put32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (8u * i));
}
static t5_stream_result_t read_record(t5_stream_t stream, void *data,
                                      uint32_t capacity, uint32_t *size) {
    assert(stream == 66 && size);
    *size = 0;
    if (!authorized) { ++probes; return T5_STREAM_DENIED; }
    if (!queued) return T5_STREAM_AGAIN;
    assert(data && capacity >= RISCRTE_LOCATION_FIX_SIZE);
    uint8_t *bytes = data;
    memset(bytes, 0, RISCRTE_LOCATION_FIX_SIZE);
    put32(bytes + RISCRTE_FIX_OFFSET_VERSION, RISCRTE_LOCATION_FIX_VERSION);
    put32(bytes + RISCRTE_FIX_OFFSET_SAMPLE_MS, 1000 + received * 250);
    put32(bytes + RISCRTE_FIX_OFFSET_FIX_MS, 988 + received * 250);
    const double latitude = 44.532385, longitude = -116.056066;
    memcpy(bytes + RISCRTE_FIX_OFFSET_LATITUDE, &latitude, sizeof(latitude));
    memcpy(bytes + RISCRTE_FIX_OFFSET_LONGITUDE, &longitude, sizeof(longitude));
    bytes[RISCRTE_FIX_OFFSET_SATELLITES] = 8;
    --queued;
    ++received;
    *size = RISCRTE_LOCATION_FIX_SIZE;
    return T5_STREAM_OK;
}
static t5_stream_result_t record_info(t5_stream_t stream, riscrte_record_info_v1 *info) {
    assert(stream == 66 && info && info->struct_size >= sizeof(*info) && authorized);
    *info = (riscrte_record_info_v1){0};
    info->struct_size = sizeof(*info);
    info->flags = T5_STREAM_READ;
    strcpy(info->schema, RISCRTE_LOCATION_FIX_SCHEMA);
    info->max_record = RISCRTE_LOCATION_FIX_SIZE;
    info->capacity_records = 4;
    info->queued_records = queued;
    info->high_water_records = peak;
    return T5_STREAM_OK;
}
static t5_stream_result_t close_stream(t5_stream_t stream) {
    assert(stream == 66);
    return T5_STREAM_OK;
}
static const riscrte_stream_api_v2 streams = {
    .v1 = {.api_version = RISCRTE_STREAM_API_VERSION_2,
           .struct_size = sizeof(riscrte_stream_api_v2), .close = close_stream},
    .record_read = read_record, .record_info = record_info,
};
const t5_stream_api_v1 *t5_stream_get_api(uint32_t version) {
    return version == RISCRTE_STREAM_API_VERSION_2 ? &streams.v1 : NULL;
}
int main(void) {
    app_main();
    assert(step == 12);
    assert(requests == 1 && releases == 1 && subscriptions == 1 && unsubscriptions == 1);
    assert(peak == 4 && produced == 5 && received == 5 && polls == 9);
    assert(probes == 1 && !authorized && !strcmp(count_text, "5"));
    assert(strstr(verdict, "PASS: revoked stream denied raw read") != NULL);
    puts("GNSS diagnostic queue backpressure/recovery/revocation test passed");
    return 0;
}
