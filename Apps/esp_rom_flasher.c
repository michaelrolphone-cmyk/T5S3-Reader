#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"
#include "esp_rom_md5.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_IMAGES 64
#define PATH_CAP 512
#define STATUS_CAP 128
#define FLASH_BLOCK 1024u
#define MAX_IMAGE_SIZE 0x01000000u
#define MIN_IMAGE_SIZE 0x00010000u

#define ESP_FLASH_BEGIN       0x02u
#define ESP_FLASH_DATA        0x03u
#define ESP_FLASH_END         0x04u
#define ESP_SYNC              0x08u
#define ESP_SPI_SET_PARAMS    0x0bu
#define ESP_SPI_ATTACH        0x0du
#define ESP_FLASH_MD5         0x13u
#define ESP_GET_SECURITY_INFO 0x14u

static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_ui_api_v1 *ui;
static const t5_usb_api_v1 *usb;

static char images[MAX_IMAGES][T5_APP_DIRENT_NAME_MAX];
static uint32_t image_count;
static int32_t selected;
static char status_text[STATUS_CAP];

static void le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8u);
    p[2] = (uint8_t)(v >> 16u);
    p[3] = (uint8_t)(v >> 24u);
}

static uint16_t le16_read(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

static bool ends_with_bin(const char *s) {
    const size_t n = s ? strlen(s) : 0;
    if (n < 4u) return false;
    const char *p = s + n - 4u;
    return p[0] == '.' && (p[1] == 'b' || p[1] == 'B') &&
           (p[2] == 'i' || p[2] == 'I') && (p[3] == 'n' || p[3] == 'N');
}

static char lower_ascii(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static void render_list(void) {
    t5_ui_list_row_t rows[MAX_IMAGES];
    for (uint32_t i = 0; i < image_count; ++i) {
        rows[i].title = images[i];
        rows[i].subtitle = NULL;
        rows[i].value = NULL;
        rows[i].flags = 0;
    }
    const t5_ui_chrome_t chrome = {
        .title = "ESP ROM Flasher",
        .subtitle = "Merged 0x0 firmware image from /sd",
        .status = image_count ? "Confirm to flash selected image" : "No .bin files found in /sd",
        .back_label = "Back",
        .confirm_label = "Flash",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, image_count, selected);
}

static void render_status(const char *subtitle, const char *value, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "ESP ROM Flasher",
        .subtitle = subtitle,
        .status = status,
        .back_label = "Back",
        .confirm_label = "OK",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t row = {
        .title = image_count ? images[selected] : "",
        .subtitle = NULL,
        .value = value,
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static bool poll_delay(uint32_t delay_ms) {
    const uint32_t start = app->millis();
    while ((uint32_t)(app->millis() - start) < delay_ms) {
        t5_app_input_t input;
        uint32_t remaining = delay_ms - (uint32_t)(app->millis() - start);
        if (remaining > 50u) remaining = 50u;
        if (!app->poll(&input, remaining)) return false;
        if (input.exit_requested) return false;
    }
    return true;
}

static void drain_rx(void) {
    uint8_t tmp[128];
    while (usb->serial_read(tmp, sizeof(tmp)) != 0u) {}
}

static bool write_all(const uint8_t *data, size_t length, uint32_t timeout_ms) {
    size_t sent = 0;
    const uint32_t start = app->millis();
    while (sent < length && (uint32_t)(app->millis() - start) < timeout_ms) {
        const size_t count = usb->serial_write(data + sent, length - sent);
        if (count != 0u) sent += count;
        t5_app_input_t input;
        if (!app->poll(&input, 5)) return false;
        if (input.exit_requested) return false;
    }
    return sent == length;
}

static size_t slip_encode(const uint8_t *input, size_t length, uint8_t *output, size_t capacity) {
    size_t out = 0;
    if (out >= capacity) return 0;
    output[out++] = 0xc0u;
    for (size_t i = 0; i < length; ++i) {
        if (input[i] == 0xc0u) {
            if (out + 2u > capacity) return 0;
            output[out++] = 0xdbu;
            output[out++] = 0xdcu;
        } else if (input[i] == 0xdbu) {
            if (out + 2u > capacity) return 0;
            output[out++] = 0xdbu;
            output[out++] = 0xddu;
        } else {
            if (out >= capacity) return 0;
            output[out++] = input[i];
        }
    }
    if (out >= capacity) return 0;
    output[out++] = 0xc0u;
    return out;
}

static bool recv_slip(uint8_t *output, size_t capacity, size_t *out_length, uint32_t timeout_ms) {
    bool started = false;
    bool escaped = false;
    size_t length = 0;
    const uint32_t start = app->millis();
    while ((uint32_t)(app->millis() - start) < timeout_ms) {
        uint8_t incoming[64];
        const size_t count = usb->serial_read(incoming, sizeof(incoming));
        for (size_t i = 0; i < count; ++i) {
            uint8_t value = incoming[i];
            if (!started) {
                if (value == 0xc0u) {
                    started = true;
                    escaped = false;
                    length = 0;
                }
                continue;
            }
            if (value == 0xc0u) {
                if (length != 0u) {
                    *out_length = length;
                    return true;
                }
                continue;
            }
            if (escaped) {
                if (value == 0xdcu) value = 0xc0u;
                else if (value == 0xddu) value = 0xdbu;
                escaped = false;
            } else if (value == 0xdbu) {
                escaped = true;
                continue;
            }
            if (length >= capacity) return false;
            output[length++] = value;
        }
        t5_app_input_t input;
        if (!app->poll(&input, 5)) return false;
        if (input.exit_requested) return false;
    }
    return false;
}

static bool rom_reply_success(uint8_t op, const uint8_t *reply, size_t reply_length) {
    if (reply_length < 12u || reply[0] != 0x01u || reply[1] != op) return false;
    const uint16_t data_length = le16_read(reply + 2);
    if (data_length < 4u || reply_length < 8u + (size_t)data_length) return false;
    /* ESP32-family ROM responses end in status,error,reserved,reserved. */
    return reply[8u + (size_t)data_length - 4u] == 0u;
}

static bool command(uint8_t op, const uint8_t *payload, uint16_t payload_length,
                    uint32_t checksum, uint8_t *reply, size_t reply_capacity,
                    size_t *reply_length, uint32_t timeout_ms) {
    uint8_t raw[20u + FLASH_BLOCK];
    if ((size_t)payload_length + 8u > sizeof(raw)) return false;
    raw[0] = 0x00u;
    raw[1] = op;
    raw[2] = (uint8_t)payload_length;
    raw[3] = (uint8_t)(payload_length >> 8u);
    le32(raw + 4, checksum);
    if (payload_length != 0u) memcpy(raw + 8, payload, payload_length);

    uint8_t framed[2u * (20u + FLASH_BLOCK) + 2u];
    const size_t framed_length = slip_encode(raw, (size_t)payload_length + 8u, framed, sizeof(framed));
    if (framed_length == 0u || !write_all(framed, framed_length, timeout_ms)) return false;

    const uint32_t start = app->millis();
    for (;;) {
        const uint32_t elapsed = (uint32_t)(app->millis() - start);
        if (elapsed >= timeout_ms) return false;
        if (!recv_slip(reply, reply_capacity, reply_length, timeout_ms - elapsed)) return false;
        /* SYNC and retried operations can leave older responses queued. */
        if (*reply_length < 2u || reply[0] != 0x01u || reply[1] != op) continue;
        return rom_reply_success(op, reply, *reply_length);
    }
}

static bool wait_usb_ready(uint32_t timeout_ms) {
    t5_usb_serial_state_t state;
    const uint32_t start = app->millis();
    while ((uint32_t)(app->millis() - start) < timeout_ms) {
        if (usb->serial_read_state(&state)) {
            if (state.status == T5_USB_STATUS_READY) return true;
            if (state.status == T5_USB_STATUS_ERROR) return false;
        }
        t5_app_input_t input;
        if (!app->poll(&input, 10)) return false;
        if (input.exit_requested) return false;
    }
    return false;
}

static bool set_control_lines_wait(bool dtr, bool rts, uint32_t settle_ms) {
    if (!usb->serial_set_control_lines(dtr, rts)) return false;
    if (!wait_usb_ready(1000u)) return false;
    return settle_ms == 0u || poll_delay(settle_ms);
}

static bool enter_bootloader(void) {
    drain_rx();
    /* Espressif tight reset sequence. DTR drives IO0 and RTS drives EN, both active-low. */
    if (!set_control_lines_wait(false, false, 20u)) return false;
    if (!set_control_lines_wait(true, true, 20u)) return false;
    if (!set_control_lines_wait(false, true, 100u)) return false; /* IO0 high, EN low */
    if (!set_control_lines_wait(true, false, 50u)) return false;  /* IO0 low, EN high */
    if (!set_control_lines_wait(false, false, 50u)) return false; /* release IO0 */
    drain_rx();
    return true;
}

static bool sync_rom(void) {
    uint8_t payload[36];
    memset(payload, 0x55, sizeof(payload));
    payload[0] = 0x07u;
    payload[1] = 0x07u;
    payload[2] = 0x12u;
    payload[3] = 0x20u;
    uint8_t reply[128];
    size_t reply_length = 0;
    for (uint32_t attempt = 0; attempt < 7u; ++attempt) {
        if (command(ESP_SYNC, payload, sizeof(payload), 0, reply, sizeof(reply), &reply_length, 500u)) return true;
        drain_rx();
    }
    return false;
}

static bool extended_flash_begin_supported(void) {
    uint8_t reply[96];
    size_t reply_length = 0;
    /* GET_SECURITY_INFO exists on S2/S3 and newer ROMs. These use the extended
       FLASH_BEGIN parameter containing the encrypted-write flag. */
    return command(ESP_GET_SECURITY_INFO, NULL, 0, 0, reply, sizeof(reply), &reply_length, 750u);
}

static uint32_t flash_capacity_for_image(size_t image_size) {
    uint32_t capacity = 2u * 1024u * 1024u;
    while (capacity < image_size && capacity < MAX_IMAGE_SIZE) capacity <<= 1u;
    return capacity >= image_size ? capacity : 0u;
}

static bool configure_flash(uint32_t capacity) {
    uint8_t reply[96];
    size_t reply_length = 0;
    uint8_t attach[8] = {0};
    if (!command(ESP_SPI_ATTACH, attach, sizeof(attach), 0,
                 reply, sizeof(reply), &reply_length, 3000u)) return false;

    uint8_t params[24];
    le32(params + 0, 0u);                 /* flash ID: let ROM use attached flash */
    le32(params + 4, capacity);
    le32(params + 8, 64u * 1024u);        /* block size */
    le32(params + 12, 4u * 1024u);        /* sector size */
    le32(params + 16, 256u);              /* page size */
    le32(params + 20, 0xffffu);           /* status mask */
    return command(ESP_SPI_SET_PARAMS, params, sizeof(params), 0,
                   reply, sizeof(reply), &reply_length, 3000u);
}

static uint8_t flash_checksum(const uint8_t *data, size_t length) {
    uint8_t value = 0xefu;
    for (size_t i = 0; i < length; ++i) value ^= data[i];
    return value;
}

static bool validate_image(const char *path, size_t *image_size) {
    size_t size = 0;
    t5_storage_stream_t stream = storage->stream_open(path, &size);
    if (stream == T5_STORAGE_STREAM_INVALID) return false;
    uint8_t header[4] = {0};
    const size_t count = storage->stream_read(stream, header, sizeof(header));
    storage->stream_close(stream);
    if (count != sizeof(header) || size < MIN_IMAGE_SIZE || size > MAX_IMAGE_SIZE) return false;
    /* A merged image flashed at offset 0 begins with an ESP image bootloader. */
    if (header[0] != 0xe9u) return false;
    *image_size = size;
    return true;
}

static bool source_md5(const char *path, size_t expected_size, char hex_out[33]) {
    size_t size = 0;
    t5_storage_stream_t stream = storage->stream_open(path, &size);
    if (stream == T5_STORAGE_STREAM_INVALID || size != expected_size) {
        if (stream != T5_STORAGE_STREAM_INVALID) storage->stream_close(stream);
        return false;
    }

    esp_rom_md5_ctx_t md5;
    esp_rom_md5_init(&md5);
    uint8_t block[FLASH_BLOCK];
    size_t remaining = size;
    while (remaining != 0u) {
        size_t wanted = remaining < sizeof(block) ? remaining : sizeof(block);
        const size_t count = storage->stream_read(stream, block, wanted);
        if (count == 0u || count > remaining) {
            storage->stream_close(stream);
            return false;
        }
        esp_rom_md5_update(&md5, block, count);
        remaining -= count;
    }
    storage->stream_close(stream);

    uint8_t digest[16];
    esp_rom_md5_final(&md5, digest);
    esp_rom_md5_hex(digest, hex_out);
    return true;
}

static bool flash_stream(const char *path, size_t image_size, bool extended_begin) {
    size_t opened_size = 0;
    t5_storage_stream_t stream = storage->stream_open(path, &opened_size);
    if (stream == T5_STORAGE_STREAM_INVALID || opened_size != image_size) {
        if (stream != T5_STORAGE_STREAM_INVALID) storage->stream_close(stream);
        return false;
    }

    const uint32_t blocks = (uint32_t)((image_size + FLASH_BLOCK - 1u) / FLASH_BLOCK);
    uint8_t begin[20];
    le32(begin + 0, (uint32_t)image_size);
    le32(begin + 4, blocks);
    le32(begin + 8, FLASH_BLOCK);
    le32(begin + 12, 0u);
    le32(begin + 16, 0u); /* unencrypted write */

    const uint32_t megabytes = ((uint32_t)image_size + 0x0fffffu) >> 20u;
    const uint32_t erase_timeout = 10000u + megabytes * 40000u;
    uint8_t reply[160];
    size_t reply_length = 0;
    const uint16_t begin_length = extended_begin ? 20u : 16u;
    if (!command(ESP_FLASH_BEGIN, begin, begin_length, 0,
                 reply, sizeof(reply), &reply_length, erase_timeout)) {
        storage->stream_close(stream);
        return false;
    }

    uint8_t block[FLASH_BLOCK];
    uint8_t payload[16u + FLASH_BLOCK];
    unsigned next_progress = 5u;
    for (uint32_t sequence = 0; sequence < blocks; ++sequence) {
        const size_t count = storage->stream_read(stream, block, sizeof(block));
        if (count == 0u && sequence + 1u < blocks) {
            storage->stream_close(stream);
            return false;
        }
        if (count < sizeof(block)) memset(block + count, 0xff, sizeof(block) - count);

        le32(payload + 0, FLASH_BLOCK);
        le32(payload + 4, sequence);
        le32(payload + 8, 0u);
        le32(payload + 12, 0u);
        memcpy(payload + 16, block, FLASH_BLOCK);

        bool written = false;
        for (uint32_t attempt = 0; attempt < 3u && !written; ++attempt) {
            written = command(ESP_FLASH_DATA, payload, sizeof(payload),
                              flash_checksum(block, FLASH_BLOCK), reply, sizeof(reply),
                              &reply_length, 5000u);
        }
        if (!written) {
            storage->stream_close(stream);
            return false;
        }

        const unsigned percent = (unsigned)(((sequence + 1u) * 100u) / blocks);
        if (percent >= next_progress || percent == 100u) {
            snprintf(status_text, sizeof(status_text), "%u%% | block %lu/%lu", percent,
                     (unsigned long)(sequence + 1u), (unsigned long)blocks);
            render_status("Flashing firmware - do not disconnect", "Writing", status_text);
            next_progress = percent + 5u;
        }
    }
    storage->stream_close(stream);
    return true;
}

static bool verify_md5(size_t image_size, const char expected_hex[33]) {
    uint8_t payload[16];
    le32(payload + 0, 0u);
    le32(payload + 4, (uint32_t)image_size);
    le32(payload + 8, 0u);
    le32(payload + 12, 0u);

    uint8_t reply[160];
    size_t reply_length = 0;
    const uint32_t megabytes = ((uint32_t)image_size + 0x0fffffu) >> 20u;
    const uint32_t timeout = 5000u + megabytes * 8000u;
    if (!command(ESP_FLASH_MD5, payload, sizeof(payload), 0,
                 reply, sizeof(reply), &reply_length, timeout)) return false;

    const uint16_t data_length = le16_read(reply + 2);
    /* ESP32-family ROM returns 32 ASCII hex characters followed by 4 status bytes. */
    if (data_length < 36u || reply_length < 8u + (size_t)data_length) return false;
    for (uint32_t i = 0; i < 32u; ++i) {
        if (lower_ascii((char)reply[8u + i]) != expected_hex[i]) return false;
    }
    return true;
}

static void reset_target(void) {
    uint8_t payload[4];
    le32(payload, 0u); /* FLASH_END 0 requests reboot. */
    uint8_t reply[64];
    size_t reply_length = 0;
    (void)command(ESP_FLASH_END, payload, sizeof(payload), 0,
                  reply, sizeof(reply), &reply_length, 1000u);

    /* Also pulse EN so an external USB-UART target leaves download mode even if
       its ROM reset response disappeared while rebooting. Keep IO0 released. */
    (void)set_control_lines_wait(false, true, 100u);
    (void)set_control_lines_wait(false, false, 50u);
}

static bool flash_selected(void) {
    char path[PATH_CAP];
    snprintf(path, sizeof(path), "/sd/%s", images[selected]);

    size_t image_size = 0;
    render_status("Checking firmware image", "Validating", "Merged ESP image required at flash offset 0x0");
    if (!validate_image(path, &image_size)) return false;

    char expected_md5[33];
    render_status("Checking firmware image", "Hashing", "Calculating source MD5");
    if (!source_md5(path, image_size, expected_md5)) return false;

    const uint32_t flash_capacity = flash_capacity_for_image(image_size);
    if (flash_capacity == 0u) return false;

    const t5_usb_line_coding_t coding = {115200u, 8u, T5_USB_PARITY_NONE, 1u, 0u};
    render_status("Connecting to target", "USB", "Powering target and opening serial bridge");
    if (!usb->serial_start(&coding)) return false;
    if (!wait_usb_ready(8000u)) {
        usb->serial_stop();
        return false;
    }
    if (!enter_bootloader()) {
        usb->serial_stop();
        return false;
    }

    render_status("Connecting to target", "ROM bootloader", "Synchronizing");
    if (!sync_rom()) {
        usb->serial_stop();
        return false;
    }

    const bool extended_begin = extended_flash_begin_supported();
    drain_rx();
    render_status("ROM bootloader connected", "SPI flash", "Configuring flash interface");
    if (!configure_flash(flash_capacity)) {
        usb->serial_stop();
        return false;
    }

    render_status("ROM bootloader connected", "Ready", "Starting flash erase/write");
    if (!flash_stream(path, image_size, extended_begin)) {
        usb->serial_stop();
        return false;
    }

    render_status("Verifying flash", "MD5", "Comparing target flash with source image");
    if (!verify_md5(image_size, expected_md5)) {
        usb->serial_stop();
        return false;
    }

    render_status("Flash complete", "Verified", "Resetting target");
    reset_target();
    usb->serial_stop();
    return true;
}

__attribute__((visibility("default"))) void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    usb = t5_usb_get_api(T5_USB_API_VERSION);
    if (!app || !storage || !ui || !usb) return;

    const size_t app_back_required =
        offsetof(t5_app_api_v1, set_back_exits_app) + sizeof(app->set_back_exits_app);
    const size_t storage_stream_required =
        offsetof(t5_storage_api_v1, stream_close) + sizeof(storage->stream_close);
    if (app->struct_size < app_back_required || storage->struct_size < storage_stream_required ||
        !app->set_back_exits_app || !app->dir_open || !app->dir_next || !app->dir_close ||
        !app->poll || !app->millis || !ui->render_list || !ui->poll_event || !ui->hit_test ||
        !ui->next_index || !ui->previous_index || !storage->stream_open || !storage->stream_read ||
        !storage->stream_close || !usb->serial_start || !usb->serial_stop ||
        !usb->serial_set_control_lines || !usb->serial_read || !usb->serial_write ||
        !usb->serial_read_state) return;

    /* Back remains ordinary app navigation, but cannot set the sticky native-app
       exit flag while erase/write/verify is in progress. */
    app->set_back_exits_app(false);

    image_count = 0;
    selected = 0;
    if (app->dir_open("/sd")) {
        t5_app_dirent_t entry;
        while (image_count < MAX_IMAGES && app->dir_next(&entry)) {
            if (!entry.is_directory && ends_with_bin(entry.name)) {
                strncpy(images[image_count], entry.name, sizeof(images[image_count]) - 1u);
                images[image_count][sizeof(images[image_count]) - 1u] = 0;
                ++image_count;
            }
        }
        app->dir_close();
    }

    render_list();
    for (;;) {
        t5_ui_event_t event;
        if (!ui->poll_event(&event, 50)) continue;
        if (event.type == T5_UI_EVENT_EXIT || event.type == T5_UI_EVENT_BACK) return;
        if (image_count == 0u) continue;

        if (event.type == T5_UI_EVENT_NEXT) {
            selected = ui->next_index(selected, image_count);
            render_list();
        } else if (event.type == T5_UI_EVENT_PREVIOUS) {
            selected = ui->previous_index(selected, image_count);
            render_list();
        } else if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit >= 0 && hit < (int32_t)image_count) {
                selected = hit;
                render_list();
            }
        } else if (event.type == T5_UI_EVENT_CONFIRM) {
            const bool ok = flash_selected();
            render_status(ok ? "Flash complete" : "Flash failed",
                          ok ? "MD5 verified" : "Error",
                          ok ? "Target reset into flashed firmware" : "Check target connection/image and retry");
            for (;;) {
                t5_ui_event_t done;
                if (!ui->poll_event(&done, 50)) continue;
                if (done.type == T5_UI_EVENT_BACK || done.type == T5_UI_EVENT_EXIT) return;
                if (done.type == T5_UI_EVENT_CONFIRM || done.type == T5_UI_EVENT_TAP) {
                    render_list();
                    break;
                }
            }
        }
    }
}
