#include "T5AppApi.h"
#include "T5ProgramEspRomApi.h"
#include "T5StreamApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_IMAGES 64u
#define PATH_CAP 512u
#define STATUS_CAP 160u

static const t5_app_api_v1 *app;
static const t5_ui_api_v1 *ui;
static const t5_stream_api_v1 *streams;
static const t5_program_esp_rom_api_v1 *programmer;
static char images[MAX_IMAGES][T5_APP_DIRENT_NAME_MAX];
static uint32_t image_sizes[MAX_IMAGES];
static uint32_t image_count;
static int32_t selected;
static char failure_text[STATUS_CAP];
static uint8_t rendered_stage;
static uint8_t rendered_percent;
static char rendered_message[T5_PROGRAM_ESP_ROM_MESSAGE_MAX];

static bool ends_with_bin(const char *name) {
    const size_t n = name ? strlen(name) : 0;
    if (n < 4) return false;
    const char *suffix = name + n - 4;
    return suffix[0] == '.' && (suffix[1] == 'b' || suffix[1] == 'B') &&
           (suffix[2] == 'i' || suffix[2] == 'I') && (suffix[3] == 'n' || suffix[3] == 'N');
}

static void render_list(void) {
    t5_ui_list_row_t rows[MAX_IMAGES];
    uint32_t i;
    for (i = 0; i < image_count; ++i) {
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

static void render_status(const char *subtitle, const char *value, const char *message) {
    const t5_ui_chrome_t chrome = {
        .title = "ESP ROM Flasher",
        .subtitle = subtitle,
        .status = message,
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
        case T5_PROGRAM_STAGE_CONNECT: return "ROM bootloader";
        case T5_PROGRAM_STAGE_CONFIGURE: return "SPI flash";
        case T5_PROGRAM_STAGE_ERASE: return "Erasing";
        case T5_PROGRAM_STAGE_WRITE: return "Writing";
        case T5_PROGRAM_STAGE_VERIFY: return "MD5";
        case T5_PROGRAM_STAGE_RESET: return "Resetting";
        case T5_PROGRAM_STAGE_COMPLETE: return "Verified";
        default: return "Status";
    }
}

/* The provider calls this synchronously and never retains an ELF callback.
 * Firmware UI keeps navigation and drawing; the provider owns serial, ROM
 * protocol, flash data buffers, source hashing and target verification. */
static bool on_progress(void *context, const t5_program_esp_rom_status_v1 *status) {
    (void)context;
    if (!status || status->struct_size < sizeof(*status)) return false;
    t5_ui_event_t event;
    if (ui->poll_event(&event, 0u) &&
        (event.type == T5_UI_EVENT_BACK || event.type == T5_UI_EVENT_EXIT)) return false;
    if (status->stage != rendered_stage ||
        (status->percent != rendered_percent &&
         (status->percent == 100u || status->percent >= rendered_percent + 5u)) ||
        strcmp(status->message, rendered_message) != 0) {
        char message[STATUS_CAP];
        if (status->stage == T5_PROGRAM_STAGE_WRITE || status->stage == T5_PROGRAM_STAGE_HASH) {
            snprintf(message, sizeof(message), "%u%% | %s", status->percent, status->message);
        } else {
            snprintf(message, sizeof(message), "%s", status->message);
        }
        render_status(stage_title(status->stage), stage_value(status->stage), message);
        rendered_stage = status->stage;
        rendered_percent = status->percent;
        strncpy(rendered_message, status->message, sizeof(rendered_message) - 1u);
        rendered_message[sizeof(rendered_message) - 1u] = 0;
    }
    return true;
}

static bool flash_selected(void) {
    char path[PATH_CAP];
    t5_stream_t firmware = 0;
    t5_program_esp_rom_status_v1 final_status;
    failure_text[0] = 0;
    rendered_stage = rendered_percent = 0;
    rendered_message[0] = 0;
    if (selected < 0 || (uint32_t)selected >= image_count) return false;
    if (snprintf(path, sizeof(path), "/sd/%s", images[selected]) >= (int)sizeof(path)) {
        snprintf(failure_text, sizeof(failure_text), "Firmware filename is too long");
        return false;
    }
    render_status("Checking firmware image", "Validating", "Merged ESP image required at flash offset 0x0");
    if (streams->open_file(path, T5_STREAM_FILE_READ, &firmware) != T5_STREAM_OK || !firmware) {
        snprintf(failure_text, sizeof(failure_text), "Cannot open firmware image");
        return false;
    }
    memset(&final_status, 0, sizeof(final_status));
    const t5_program_esp_rom_result_t result = programmer->program(
        firmware, image_sizes[selected], on_progress, NULL, &final_status);
    (void)streams->close(firmware);
    if (result != T5_PROGRAM_OK) {
        if (final_status.struct_size >= sizeof(final_status) && final_status.message[0]) {
            snprintf(failure_text, sizeof(failure_text), "%s", final_status.message);
        } else {
            snprintf(failure_text, sizeof(failure_text), "Programming failed (error %ld)", (long)result);
        }
        return false;
    }
    return true;
}

__attribute__((visibility("default"))) void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    streams = t5_stream_get_api(T5_STREAM_API_VERSION);
    programmer = t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION);
    if (!app || !ui || !streams || !programmer ||
        app->struct_size < offsetof(t5_app_api_v1, set_back_exits_app) + sizeof(app->set_back_exits_app) ||
        streams->struct_size < offsetof(t5_stream_api_v1, close) + sizeof(streams->close) ||
        programmer->struct_size < offsetof(t5_program_esp_rom_api_v1, program) + sizeof(programmer->program) ||
        !programmer->capability_id || strcmp(programmer->capability_id, T5_PROGRAM_ESP_ROM_CAPABILITY) != 0 ||
        !app->set_back_exits_app || !app->dir_open || !app->dir_next || !app->dir_close ||
        !ui->render_list || !ui->poll_event || !ui->hit_test || !ui->next_index ||
        !ui->previous_index || !streams->open_file || !streams->close || !programmer->program) return;
    app->set_back_exits_app(false);
    image_count = 0;
    selected = 0;
    if (app->dir_open("/sd")) {
        t5_app_dirent_t entry;
        while (image_count < MAX_IMAGES && app->dir_next(&entry)) {
            if (!entry.is_directory && ends_with_bin(entry.name)) {
                strncpy(images[image_count], entry.name, sizeof(images[image_count]) - 1u);
                images[image_count][sizeof(images[image_count]) - 1u] = 0;
                image_sizes[image_count] = entry.size <= UINT32_MAX ? (uint32_t)entry.size : 0u;
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
            const bool ok = flash_selected();
            render_status(ok ? "Flash complete" : "Flash failed",
                          ok ? "MD5 verified" : "Error",
                          ok ? "Target reset into flashed firmware" :
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
