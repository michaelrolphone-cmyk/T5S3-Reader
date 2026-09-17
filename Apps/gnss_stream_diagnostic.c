#include "T5AppApi.h"
#include "T5DeviceApi.h"
#include "T5GpsApi.h"
#include "T5LocationApi.h"
#include "T5StreamApi.h"
#include "T5UiApi.h"
#include "RiscRteLocationRecords.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const t5_ui_api_v1 *ui;
static char status_text[96];
static char count_text[24];
static char lat_text[32];
static char lon_text[32];
static char satellites_text[16];
static char age_text[24];

static void show(void) {
    const t5_ui_chrome_t chrome = {
        .title = "GNSS Stream Diagnostic",
        .subtitle = "Authorized location.fix.v1",
        .status = status_text,
        .back_label = "Back",
        .confirm_label = "",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t rows[] = {
        {.title = "Stream", .value = status_text, .flags = 0},
        {.title = "Records", .value = count_text, .flags = 0},
        {.title = "Latitude", .value = lat_text, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "Longitude", .value = lon_text, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "Satellites", .value = satellites_text, .flags = 0},
        {.title = "Fix age (ms)", .value = age_text, .flags = 0},
    };
    ui->render_list(&chrome, rows, (uint32_t)(sizeof(rows) / sizeof(rows[0])), 0);
}

static void status(const char *message) {
    snprintf(status_text, sizeof(status_text), "%s", message);
    show();
}

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

// The ESP32 native ELF target and location.fix.v1 wire format are both
// little-endian IEEE-754. Copy the bits directly: variable uint64_t shifts
// would import __ashldi3, which is intentionally absent from the ELF ABI.
static double read_f64(const uint8_t *bytes) {
    double value = 0;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

// Validate finite, bounded IEEE-754 coordinates by their absolute-value bit
// patterns. Software double comparisons import __gedf2/__ledf2 on this target.
// +90.0 = 0x4056800000000000; +180.0 = 0x4066800000000000.
static bool coordinate_in_range(const uint8_t *bytes, uint32_t limit_high) {
    const uint32_t magnitude_high = read_u32(bytes + 4) & UINT32_C(0x7fffffff);
    const uint32_t magnitude_low = read_u32(bytes);
    return magnitude_high < limit_high ||
           (magnitude_high == limit_high && magnitude_low == 0);
}

static bool decode(const uint8_t *bytes, uint32_t size, uint32_t *age) {
    if (!bytes || !age || size != RISCRTE_LOCATION_FIX_SIZE ||
        read_u32(bytes + RISCRTE_FIX_OFFSET_VERSION) != RISCRTE_LOCATION_FIX_VERSION) return false;
    if (!coordinate_in_range(bytes + RISCRTE_FIX_OFFSET_LATITUDE, UINT32_C(0x40568000)) ||
        !coordinate_in_range(bytes + RISCRTE_FIX_OFFSET_LONGITUDE, UINT32_C(0x40668000))) return false;
    const double latitude = read_f64(bytes + RISCRTE_FIX_OFFSET_LATITUDE);
    const double longitude = read_f64(bytes + RISCRTE_FIX_OFFSET_LONGITUDE);
    snprintf(lat_text, sizeof(lat_text), "%.7f", latitude);
    snprintf(lon_text, sizeof(lon_text), "%.7f", longitude);
    snprintf(satellites_text, sizeof(satellites_text), "%u",
             (unsigned)bytes[RISCRTE_FIX_OFFSET_SATELLITES]);
    *age = read_u32(bytes + RISCRTE_FIX_OFFSET_SAMPLE_MS) -
           read_u32(bytes + RISCRTE_FIX_OFFSET_FIX_MS);
    snprintf(age_text, sizeof(age_text), "%lu", (unsigned long)*age);
    return true;
}

static t5_device_handle_t find_receiver(const t5_device_api_v3 *devices) {
    t5_device_info_t entries[12] = {0};
    uint32_t count = 0;
    if (devices->v2.v1.inventory(entries, 12, &count) != T5_DEVICE_OK) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].provider, "gps-nmea") != 0 ||
            entries[i].transport != T5_DEVICE_TRANSPORT_UART ||
            entries[i].state != T5_DEVICE_AVAILABLE) continue;
        for (uint32_t c = 0; c < entries[i].capability_count; ++c)
            if (strcmp(entries[i].capabilities[c], "location.position") == 0)
                return entries[i].handle;
    }
    return 0;
}

void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !app->poll || !app->millis || !ui || !ui->render_list) return;
    snprintf(count_text, sizeof(count_text), "0");
    snprintf(lat_text, sizeof(lat_text), "--");
    snprintf(lon_text, sizeof(lon_text), "--");
    snprintf(satellites_text, sizeof(satellites_text), "--");
    snprintf(age_text, sizeof(age_text), "--");
    status("Discovering GNSS device");

    const t5_device_api_v1 *device_base = t5_device_get_api(T5_DEVICE_API_VERSION_3);
    const riscrte_stream_api_v2 *streams = riscrte_stream_get_api_v2();
    const t5_location_api_v1 *location = t5_location_get_api(T5_LOCATION_API_VERSION);
    if (!device_base || device_base->api_version != T5_DEVICE_API_VERSION_3 ||
        device_base->struct_size < sizeof(t5_device_api_v3) || !streams || !location ||
        location->api_version != T5_LOCATION_API_VERSION ||
        location->struct_size < sizeof(t5_location_api_v1) ||
        !location->subscribe || !location->poll || !location->unsubscribe ||
        !streams->record_read || !streams->record_info || !streams->v1.close) {
        status("Required runtime APIs unavailable");
        return;
    }
    const t5_device_api_v3 *devices = (const t5_device_api_v3 *)(const void *)device_base;
    if (!devices->request || !devices->v2.release || !devices->v2.v1.inventory) {
        status("Device permissions unavailable");
        return;
    }

    // Compatibility discovery only: supported() publishes the board receiver
    // in the Unified Device Registry; no legacy GPS start/read occurs here.
    const t5_gps_api_v1 *gps = t5_gps_get_api(T5_GPS_API_VERSION);
    if (gps && gps->supported) (void)gps->supported();
    const t5_device_handle_t receiver = find_receiver(devices);
    if (!receiver) { status("GNSS driver unavailable"); return; }

    status("Request location READ permission");
    t5_device_lease_t authorization = 0;
    if (devices->request("location.position", receiver, T5_DEVICE_RIGHT_READ,
                         &authorization) != T5_DEVICE_OK || !authorization) {
        status("Location permission denied");
        return;
    }
    t5_location_subscription_t subscription = 0;
    t5_stream_t stream = 0;
    if (location->subscribe(authorization, &subscription, &stream) != T5_STREAM_OK ||
        !subscription || !stream) {
        status("GNSS stream unavailable");
        (void)devices->v2.release(authorization);
        return;
    }
    riscrte_record_info_v1 info = {0};
    info.struct_size = sizeof(info);
    if (streams->record_info(stream, &info) != T5_STREAM_OK ||
        strcmp(info.schema, RISCRTE_LOCATION_FIX_SCHEMA) != 0 ||
        (info.flags & T5_STREAM_READ) == 0 ||
        (info.flags & T5_STREAM_WRITE) != 0) {
        status("Unexpected stream schema/rights");
        (void)location->unsubscribe(subscription);
        (void)devices->v2.release(authorization);
        return;
    }

    status("Waiting for a GNSS fix");
    uint32_t received = 0;
    uint32_t last_poll = app->millis();
    uint32_t last_render = last_poll;
    for (;;) {
        t5_app_input_t input = {0};
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;
        const uint32_t now = app->millis();
        if (now - last_poll >= 250u) {
            last_poll = now;
            const t5_stream_result_t result = location->poll(authorization);
            if (result != T5_STREAM_OK && result != T5_STREAM_AGAIN) {
                snprintf(status_text, sizeof(status_text), "GNSS poll error %ld", (long)result);
                break;
            }
            for (unsigned n = 0; n < 8; ++n) {
                uint8_t bytes[RISCRTE_LOCATION_FIX_SIZE] = {0};
                uint32_t size = 0;
                const t5_stream_result_t read = streams->record_read(stream, bytes, sizeof(bytes), &size);
                if (read == T5_STREAM_AGAIN) break;
                if (read != T5_STREAM_OK) {
                    snprintf(status_text, sizeof(status_text), "Record read error %ld", (long)read);
                    goto done;
                }
                uint32_t age = 0;
                if (!decode(bytes, size, &age)) {
                    snprintf(status_text, sizeof(status_text), "Invalid location.fix.v1 record");
                    goto done;
                }
                ++received;
                snprintf(count_text, sizeof(count_text), "%lu", (unsigned long)received);
                snprintf(status_text, sizeof(status_text), "Live GNSS records");
            }
        }
        if (now - last_render >= 2000u) {
            show();
            last_render = now;
        }
    }
done:
    show();
    (void)location->unsubscribe(subscription);
    (void)devices->v2.release(authorization);
}
