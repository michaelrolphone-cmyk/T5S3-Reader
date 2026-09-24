#include "T5AppApi.h"
#include "T5ProgramEspRomApi.h"
#include "T5ProviderCapabilityApi.h"
#include "T5StreamApi.h"
#include "T5UiApi.h"
#include "RiscProgramMspV1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_IMAGES 64u
#define PATH_CAP 512u
#define STATUS_CAP 192u
#define TOKEN_CAP 24u
#define READER_BYTES 128u
#define MSP_IMAGE_MAX (1024u * 1024u)

typedef enum {
    IMAGE_ESP_BIN = 1,
    IMAGE_MSP_TITXT = 2,
} image_kind_t;

typedef struct {
    t5_stream_t stream;
    uint8_t data[READER_BYTES];
    uint32_t offset;
    uint32_t length;
    bool eof;
    bool failed;
} token_reader_t;

static const t5_app_api_v1 *app;
static const t5_ui_api_v1 *ui;
static const t5_stream_api_v1 *streams;
static const t5_program_esp_rom_api_v1 *esp_programmer;
static const t5_provider_capability_api_v1 *provider_caps;
static char images[MAX_IMAGES][T5_APP_DIRENT_NAME_MAX];
static uint32_t image_sizes[MAX_IMAGES];
static uint8_t image_kinds[MAX_IMAGES];
static uint32_t image_count;
static int32_t selected;
static char failure_text[STATUS_CAP];
static uint8_t rendered_stage;
static uint8_t rendered_percent;
static char rendered_message[T5_PROGRAM_ESP_ROM_MESSAGE_MAX];

static bool suffix(const char *name, const char *ending) {
    const size_t n = name ? strlen(name) : 0u;
    const size_t e = ending ? strlen(ending) : 0u;
    if (!e || n < e) return false;
    const char *s = name + n - e;
    for (size_t i = 0; i < e; ++i) {
        char a = s[i], b = ending[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

static const char *kind_label(uint8_t kind) {
    return kind == IMAGE_MSP_TITXT ? "MSP430FR TI-TXT | MSP-FET/eZ-FET" :
                                     "ESP merged BIN | serial/FTDI";
}

static bool user_cancelled(void) {
    t5_ui_event_t event;
    return ui->poll_event(&event, 0u) &&
           (event.type == T5_UI_EVENT_BACK || event.type == T5_UI_EVENT_EXIT);
}

static void render_list(void) {
    t5_ui_list_row_t rows[MAX_IMAGES];
    for (uint32_t i = 0; i < image_count; ++i) {
        rows[i].title = images[i];
        rows[i].subtitle = kind_label(image_kinds[i]);
        rows[i].value = NULL;
        rows[i].flags = 0;
    }
    const t5_ui_chrome_t chrome = {
        .title = "Firmware Flasher",
        .subtitle = "ESP via serial/FTDI | MSP430FR via MSP-FET",
        .status = image_count ? "Confirm to flash selected image" :
                                "No .bin or TI-TXT .txt images found in /sd",
        .back_label = "Back",
        .confirm_label = "Flash",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, image_count, selected);
}

static void render_status(const char *subtitle, const char *value, const char *message) {
    const t5_ui_chrome_t chrome = {
        .title = "Firmware Flasher",
        .subtitle = subtitle,
        .status = message,
        .back_label = "Back",
        .confirm_label = "OK",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t row = {
        .title = image_count ? images[selected] : "",
        .subtitle = image_count ? kind_label(image_kinds[selected]) : NULL,
        .value = value,
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static const char *stage_title(uint8_t stage) {
    switch (stage) {
        case T5_PROGRAM_STAGE_VALIDATE: return "Checking firmware image";
        case T5_PROGRAM_STAGE_HASH: return "Checking firmware image";
        case T5_PROGRAM_STAGE_CONNECT: return "Connecting to target";
        case T5_PROGRAM_STAGE_CONFIGURE: return "ROM bootloader connected";
        case T5_PROGRAM_STAGE_ERASE: return "Flashing firmware - do not disconnect";
        case T5_PROGRAM_STAGE_WRITE: return "Flashing firmware - do not disconnect";
        case T5_PROGRAM_STAGE_VERIFY: return "Verifying flash";
        case T5_PROGRAM_STAGE_RESET: return "Flash complete";
        case T5_PROGRAM_STAGE_COMPLETE: return "Flash complete";
        default: return "Programming";
    }
}

static const char *stage_value(uint8_t stage) {
    switch (stage) {
        case T5_PROGRAM_STAGE_VALIDATE: return "Validating";
        case T5_PROGRAM_STAGE_HASH: return "Hashing";
        case T5_PROGRAM_STAGE_CONNECT: return "ESP ROM";
        case T5_PROGRAM_STAGE_CONFIGURE: return "SPI flash";
        case T5_PROGRAM_STAGE_ERASE: return "Erasing";
        case T5_PROGRAM_STAGE_WRITE: return "Writing";
        case T5_PROGRAM_STAGE_VERIFY: return "MD5";
        case T5_PROGRAM_STAGE_RESET: return "Resetting";
        case T5_PROGRAM_STAGE_COMPLETE: return "Verified";
        default: return "Status";
    }
}

static bool on_progress(void *context, const t5_program_esp_rom_status_v1 *status) {
    (void)context;
    if (!status || status->struct_size < sizeof(*status) || user_cancelled()) return false;
    if (status->stage != rendered_stage ||
        (status->percent != rendered_percent &&
         (status->percent == 100u || status->percent >= rendered_percent + 5u)) ||
        strcmp(status->message, rendered_message) != 0) {
        char message[STATUS_CAP];
        if (status->stage == T5_PROGRAM_STAGE_WRITE || status->stage == T5_PROGRAM_STAGE_HASH)
            snprintf(message, sizeof(message), "%u%% | %s", status->percent, status->message);
        else
            snprintf(message, sizeof(message), "%s", status->message);
        render_status(stage_title(status->stage), stage_value(status->stage), message);
        rendered_stage = status->stage;
        rendered_percent = status->percent;
        strncpy(rendered_message, status->message, sizeof(rendered_message) - 1u);
        rendered_message[sizeof(rendered_message) - 1u] = 0;
    }
    return true;
}

static bool flash_esp_selected(void) {
    char path[PATH_CAP];
    t5_stream_t firmware = 0;
    t5_program_esp_rom_status_v1 final_status;
    failure_text[0] = 0;
    rendered_stage = rendered_percent = 0;
    rendered_message[0] = 0;
    if (!esp_programmer || selected < 0 || (uint32_t)selected >= image_count) return false;
    if (snprintf(path, sizeof(path), "/sd/%s", images[selected]) >= (int)sizeof(path)) {
        snprintf(failure_text, sizeof(failure_text), "Firmware filename is too long");
        return false;
    }
    render_status("Checking ESP firmware", "Validating",
                  "Merged ESP image at 0x0; serial.port may be FTDI/CDC/CP210x/CH34x");
    if (streams->open_file(path, T5_STREAM_FILE_READ, &firmware) != T5_STREAM_OK || !firmware) {
        snprintf(failure_text, sizeof(failure_text), "Cannot open firmware image");
        return false;
    }
    memset(&final_status, 0, sizeof(final_status));
    const t5_program_esp_rom_result_t result = esp_programmer->program(
        firmware, image_sizes[selected], on_progress, NULL, &final_status);
    (void)streams->close(firmware);
    if (result != T5_PROGRAM_OK) {
        if (final_status.struct_size >= sizeof(final_status) && final_status.message[0])
            snprintf(failure_text, sizeof(failure_text), "%s", final_status.message);
        else
            snprintf(failure_text, sizeof(failure_text), "ESP programming failed (error %ld)", (long)result);
        return false;
    }
    return true;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_u32(const char *text, uint32_t *value) {
    if (!text || !*text || !value) return false;
    uint32_t out = 0;
    size_t digits = 0;
    while (*text) {
        const int h = hex_value(*text++);
        if (h < 0 || digits >= 5u) return false;
        out = (out << 4) | (uint32_t)h;
        ++digits;
    }
    if (!digits || out > 0x000fffffu) return false;
    *value = out;
    return true;
}

static int reader_get(token_reader_t *reader, char *out) {
    if (!reader || !out || reader->failed) return -1;
    for (unsigned attempt = 0; attempt < 16u; ++attempt) {
        if (reader->offset < reader->length) {
            *out = (char)reader->data[reader->offset++];
            return 1;
        }
        if (reader->eof) return 0;
        reader->offset = reader->length = 0;
        uint32_t count = 0;
        const t5_stream_result_t rc = streams->read(
            reader->stream, reader->data, sizeof(reader->data), &count);
        if (count > sizeof(reader->data)) {
            reader->failed = true;
            return -1;
        }
        reader->length = count;
        if (rc == T5_STREAM_EOF) reader->eof = true;
        else if (rc < 0) {
            reader->failed = true;
            return -1;
        }
        if (count) continue;
        if (user_cancelled()) {
            reader->failed = true;
            snprintf(failure_text, sizeof(failure_text), "Programming cancelled");
            return -1;
        }
    }
    reader->failed = true;
    snprintf(failure_text, sizeof(failure_text), "Firmware input stalled");
    return -1;
}

static int next_token(token_reader_t *reader, char token[TOKEN_CAP]) {
    char c = 0;
    int rc;
    do {
        rc = reader_get(reader, &c);
        if (rc <= 0) return rc;
    } while (c == ' ' || c == '\t' || c == '\r' || c == '\n');

    size_t length = 0;
    do {
        if (length + 1u >= TOKEN_CAP) {
            snprintf(failure_text, sizeof(failure_text), "TI-TXT token is too long");
            reader->failed = true;
            return -1;
        }
        token[length++] = c;
        rc = reader_get(reader, &c);
        if (rc < 0) return -1;
        if (!rc) break;
    } while (c != ' ' && c != '\t' && c != '\r' && c != '\n');
    token[length] = 0;
    return 1;
}

static bool scan_titxt(t5_stream_t stream, uint32_t *total_bytes) {
    if (!total_bytes || streams->seek(stream, 0) != T5_STREAM_OK) return false;
    token_reader_t reader = {.stream = stream};
    char token[TOKEN_CAP];
    bool have_address = false, terminated = false;
    uint32_t address = 0, total = 0;

    for (uint32_t tokens = 0; tokens < 2u * MSP_IMAGE_MAX + 1024u; ++tokens) {
        const int rc = next_token(&reader, token);
        if (rc < 0) return false;
        if (!rc) break;
        if (token[0] == '@') {
            if (!parse_hex_u32(token + 1, &address)) {
                snprintf(failure_text, sizeof(failure_text), "Invalid TI-TXT address");
                return false;
            }
            have_address = true;
            continue;
        }
        if ((token[0] == 'q' || token[0] == 'Q') && token[1] == 0) {
            terminated = true;
            break;
        }
        if (!have_address || token[0] == 0 || token[1] == 0 || token[2] != 0) {
            snprintf(failure_text, sizeof(failure_text), "Invalid TI-TXT data token");
            return false;
        }
        const int hi = hex_value(token[0]), lo = hex_value(token[1]);
        if (hi < 0 || lo < 0 || address >= 0x00100000u || total >= MSP_IMAGE_MAX) {
            snprintf(failure_text, sizeof(failure_text), "TI-TXT address/data exceeds MSP bounds");
            return false;
        }
        (void)hi; (void)lo;
        ++address;
        ++total;
    }
    if (!terminated || !total) {
        snprintf(failure_text, sizeof(failure_text), "TI-TXT must contain addressed data and terminating q");
        return false;
    }
    *total_bytes = total;
    return streams->seek(stream, 0) == T5_STREAM_OK;
}

static bool msp_error(const risc_program_msp_api_v1 *msp, const char *fallback) {
    char detail[RISC_PROGRAM_MSP_ERROR_MAX] = {0};
    if (msp && msp->last_error && msp->last_error(msp->context, detail, sizeof(detail)) && detail[0])
        snprintf(failure_text, sizeof(failure_text), "%s", detail);
    else
        snprintf(failure_text, sizeof(failure_text), "%s", fallback);
    return false;
}

static bool flush_msp_chunk(const risc_program_msp_api_v1 *msp, uint64_t session,
                            uint32_t address, const uint8_t *data, size_t length,
                            uint32_t *done, uint32_t total) {
    if (!length) return true;
    if (msp->write(msp->context, session, address, data, length) != (int32_t)length)
        return msp_error(msp, "MSP FRAM write failed");
    if (msp->verify(msp->context, session, address, data, length) != (int32_t)length)
        return msp_error(msp, "MSP FRAM verification failed");
    *done += (uint32_t)length;
    const unsigned percent = total ? (unsigned)(((uint64_t)*done * 100u) / total) : 0u;
    char message[STATUS_CAP];
    snprintf(message, sizeof(message), "%u%% | %lu/%lu bytes verified",
             percent, (unsigned long)*done, (unsigned long)total);
    render_status("Programming MSP430FR - do not disconnect", "Write + verify", message);
    return !user_cancelled() || (snprintf(failure_text, sizeof(failure_text), "Programming cancelled"), false);
}

static bool program_titxt(const risc_program_msp_api_v1 *msp, uint64_t session,
                          t5_stream_t stream, uint32_t total) {
    if (streams->seek(stream, 0) != T5_STREAM_OK) return false;
    token_reader_t reader = {.stream = stream};
    char token[TOKEN_CAP];
    uint8_t chunk[RISC_PROGRAM_MSP_MAX_CHUNK];
    size_t chunk_length = 0;
    uint32_t chunk_address = 0, address = 0, done = 0;
    bool have_address = false;

    for (uint32_t tokens = 0; tokens < 2u * MSP_IMAGE_MAX + 1024u; ++tokens) {
        const int rc = next_token(&reader, token);
        if (rc < 0) return false;
        if (!rc) break;

        if (token[0] == '@') {
            if (chunk_length &&
                !flush_msp_chunk(msp, session, chunk_address, chunk, chunk_length, &done, total))
                return false;
            chunk_length = 0;
            if (!parse_hex_u32(token + 1, &address)) return false;
            have_address = true;
            continue;
        }
        if ((token[0] == 'q' || token[0] == 'Q') && token[1] == 0) {
            if (chunk_length &&
                !flush_msp_chunk(msp, session, chunk_address, chunk, chunk_length, &done, total))
                return false;
            return done == total;
        }

        if (!have_address || token[2] != 0) return false;
        const int hi = hex_value(token[0]), lo = hex_value(token[1]);
        if (hi < 0 || lo < 0) return false;
        if (!chunk_length) chunk_address = address;
        if (address != chunk_address + chunk_length ||
            chunk_length == sizeof(chunk)) {
            if (!flush_msp_chunk(msp, session, chunk_address, chunk, chunk_length, &done, total))
                return false;
            chunk_length = 0;
            chunk_address = address;
        }
        chunk[chunk_length++] = (uint8_t)((hi << 4) | lo);
        ++address;
    }
    snprintf(failure_text, sizeof(failure_text), "TI-TXT ended unexpectedly");
    return false;
}

static bool flash_msp_selected(void) {
    char path[PATH_CAP];
    t5_stream_t firmware = 0;
    t5_provider_capability_lease_t provider_lease = 0;
    const void *iface = NULL;
    const risc_program_msp_api_v1 *msp = NULL;
    uint64_t session = 0;
    bool ok = false;
    failure_text[0] = 0;

    if (!provider_caps || selected < 0 || (uint32_t)selected >= image_count) {
        snprintf(failure_text, sizeof(failure_text), "MSP programming capability is unavailable");
        return false;
    }
    if (snprintf(path, sizeof(path), "/sd/%s", images[selected]) >= (int)sizeof(path)) {
        snprintf(failure_text, sizeof(failure_text), "Firmware filename is too long");
        return false;
    }
    if (streams->open_file(path, T5_STREAM_FILE_READ, &firmware) != T5_STREAM_OK || !firmware) {
        snprintf(failure_text, sizeof(failure_text), "Cannot open MSP firmware image");
        return false;
    }

    uint32_t total = 0;
    render_status("Checking MSP firmware", "TI-TXT", "Validating addressed MSP430FR image");
    if (!scan_titxt(firmware, &total)) goto cleanup;

    if (!provider_caps->acquire("program.msp", RISC_PROGRAM_MSP_API_V1,
                                &provider_lease, &iface) ||
        !provider_lease || !iface) {
        char detail[STATUS_CAP] = {0};
        if (provider_caps->last_error &&
            provider_caps->last_error(detail, sizeof(detail)) && detail[0])
            snprintf(failure_text, sizeof(failure_text), "%s", detail);
        else
            snprintf(failure_text, sizeof(failure_text), "program.msp provider is not installed");
        goto cleanup;
    }
    msp = (const risc_program_msp_api_v1 *)iface;
    if (msp->api_version != RISC_PROGRAM_MSP_API_V1 ||
        msp->struct_size < sizeof(*msp) || !msp->open || !msp->write ||
        !msp->verify || !msp->close) {
        snprintf(failure_text, sizeof(failure_text), "program.msp provider ABI is invalid");
        goto cleanup;
    }

    risc_program_msp_target_v1 target = {0};
    render_status("Connecting to MSP target", "MSP-FET/eZ-FET", "Trying Spy-Bi-Wire, then JTAG");
    session = msp->open(msp->context, 0, RISC_PROGRAM_MSP_INTERFACE_AUTO, &target);
    if (!session) {
        (void)msp_error(msp, "Cannot open MSP target");
        goto cleanup;
    }

    char target_message[STATUS_CAP];
    snprintf(target_message, sizeof(target_message),
             "JTAG ID %02X | FET protocol %u.%u | %lu bytes",
             target.jtag_id, target.protocol_major, target.protocol_minor,
             (unsigned long)total);
    render_status("MSP430FR target connected", "FRAM write + readback", target_message);

    ok = program_titxt(msp, session, firmware, total);
    if (ok)
        render_status("MSP flash complete", "Verified", "All TI-TXT bytes match target FRAM");

cleanup:
    if (session && msp && !msp->close(msp->context, session) && ok) {
        ok = false;
        (void)msp_error(msp, "MSP probe cleanup failed");
    }
    if (provider_lease && (!provider_caps->release(provider_lease)) && ok) {
        ok = false;
        snprintf(failure_text, sizeof(failure_text), "MSP provider did not quiesce");
    }
    if (firmware) (void)streams->close(firmware);
    return ok;
}

__attribute__((visibility("default"))) void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    streams = t5_stream_get_api(T5_STREAM_API_VERSION);
    esp_programmer = t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION);
    provider_caps = t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if (!app || !ui || !streams || !esp_programmer ||
        app->struct_size < offsetof(t5_app_api_v1, set_back_exits_app) + sizeof(app->set_back_exits_app) ||
        streams->struct_size < offsetof(t5_stream_api_v1, info) + sizeof(streams->info) ||
        esp_programmer->struct_size < offsetof(t5_program_esp_rom_api_v1, program) + sizeof(esp_programmer->program) ||
        !esp_programmer->capability_id ||
        strcmp(esp_programmer->capability_id, T5_PROGRAM_ESP_ROM_CAPABILITY) != 0 ||
        !app->set_back_exits_app || !app->dir_open || !app->dir_next || !app->dir_close ||
        !ui->render_list || !ui->poll_event || !ui->hit_test || !ui->next_index ||
        !ui->previous_index || !streams->open_file || !streams->read || !streams->seek ||
        !streams->close || !esp_programmer->program) return;

    app->set_back_exits_app(false);
    image_count = 0;
    selected = 0;
    if (app->dir_open("/sd")) {
        t5_app_dirent_t entry;
        while (image_count < MAX_IMAGES && app->dir_next(&entry)) {
            uint8_t kind = 0;
            if (!entry.is_directory && suffix(entry.name, ".bin")) kind = IMAGE_ESP_BIN;
            else if (!entry.is_directory && suffix(entry.name, ".txt")) kind = IMAGE_MSP_TITXT;
            if (kind) {
                strncpy(images[image_count], entry.name, sizeof(images[image_count]) - 1u);
                images[image_count][sizeof(images[image_count]) - 1u] = 0;
                image_sizes[image_count] = entry.size <= UINT32_MAX ? (uint32_t)entry.size : 0u;
                image_kinds[image_count] = kind;
                ++image_count;
            }
        }
        app->dir_close();
    }

    render_list();
    for (;;) {
        t5_ui_event_t event;
        if (!ui->poll_event(&event, 50u)) continue;
        if (event.type == T5_UI_EVENT_EXIT || event.type == T5_UI_EVENT_BACK) return;
        if (!image_count) continue;
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
            const bool msp = image_kinds[selected] == IMAGE_MSP_TITXT;
            const bool ok = msp ? flash_msp_selected() : flash_esp_selected();
            render_status(ok ? "Flash complete" : "Flash failed",
                          ok ? "Verified" : "Error",
                          ok ? (msp ? "MSP430FR image verified" :
                                      "ESP image verified and target reset") :
                               (failure_text[0] ? failure_text : "Unknown flasher failure"));
            for (;;) {
                t5_ui_event_t done;
                if (!ui->poll_event(&done, 50u)) continue;
                if (done.type == T5_UI_EVENT_BACK || done.type == T5_UI_EVENT_EXIT) return;
                if (done.type == T5_UI_EVENT_CONFIRM || done.type == T5_UI_EVENT_TAP) {
                    render_list();
                    break;
                }
            }
        }
    }
}
