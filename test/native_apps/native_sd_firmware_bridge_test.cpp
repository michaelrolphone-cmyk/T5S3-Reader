#include <assert.h>

#include <string>

#include "Arduino.h"
#include "HalStorage.h"
#include "T5AppApi.h"
#include "T5SdFirmwareApi.h"
#include "esp_ota_ops.h"
#include "network/FirmwareFlasher.h"
#include "NativeSdFirmwareBridge.h"

static bool app_api_available = true;
static bool active_source_available = true;
static std::string active_source_path = "/firmware/selected.bin";
static std::string validator_path;
static std::string install_path;
static size_t validator_partition_size;
static int validator_calls;
static int install_calls;
static int restart_calls;
static int progress_calls;
static firmware_flash::Result validator_result = firmware_flash::Result::OK;
static firmware_flash::Result install_result = firmware_flash::Result::OK;

std::string sd_firmware_opened_path;
std::string sd_firmware_opened_system;
size_t sd_firmware_file_size = 128;
bool sd_firmware_file_open_succeeds = true;
int sd_firmware_file_close_count;
bool sd_firmware_partition_available = true;
size_t sd_firmware_partition_size = 4096;
sd_firmware_fake_storage_t Storage;
sd_firmware_fake_esp_t ESP;

static const t5_app_api_v1 app_api = {T5_APP_ABI_VERSION, sizeof(t5_app_api_v1)};

extern "C" const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return app_api_available && version == T5_APP_ABI_VERSION ? &app_api : nullptr;
}

bool sd_firmware_fake_storage_t::openFileForRead(const char *system, const char *path,
                                                  HalFile &file) {
    sd_firmware_opened_system = system ? system : "";
    sd_firmware_opened_path = path ? path : "";
    file.is_open = sd_firmware_file_open_succeeds;
    return sd_firmware_file_open_succeeds;
}

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) {
    static esp_partition_t partition{};
    if (!sd_firmware_partition_available) return nullptr;
    partition.size = sd_firmware_partition_size;
    return &partition;
}

void sd_firmware_fake_esp_t::restart() { ++restart_calls; }

namespace NativeFileOpenBridge {
bool activeSourceStoragePath(std::string &out) {
    out = active_source_available ? active_source_path : "";
    return active_source_available;
}
}  // namespace NativeFileOpenBridge

namespace firmware_flash {
Result validateImageFile(const char *path, size_t partition_size) {
    ++validator_calls;
    validator_path = path ? path : "";
    validator_partition_size = partition_size;
    return validator_path.empty() ? Result::OPEN_FAIL : validator_result;
}

Result flashFromSdPath(const char *path, ProgressCb callback, void *ctx, bool) {
    ++install_calls;
    install_path = path ? path : "";
    if (callback) callback(64, 128, ctx);
    return install_result;
}
}  // namespace firmware_flash

#include "../../src/native/NativeSdFirmwareBridge.cpp"

static void progress_callback(void *) { ++progress_calls; }

int main() {
    const t5_sd_firmware_api_v1 *api = t5_sd_firmware_get_api(T5_SD_FIRMWARE_API_VERSION);
    assert(api && api->selected_path && api->validate && api->install);

    char selected[T5_SD_FIRMWARE_PATH_MAX] = {};
    assert(api->selected_path(selected, sizeof(selected)));
    assert(std::string(selected) == active_source_path);

    /* Normal File Browser handoff: only activeSourceStoragePath supplies the path. */
    assert(api->validate() == T5_SD_FIRMWARE_OK);
    assert(sd_firmware_opened_path == active_source_path);
    assert(sd_firmware_opened_system == "FW");
    assert(validator_path == active_source_path);
    assert(validator_partition_size == sd_firmware_partition_size);
    assert(api->image_size() == sd_firmware_file_size);
    assert(sd_firmware_file_close_count == 1);

    /* An explicit legacy selection still takes precedence over the active handoff. */
    NativeSdFirmwareBridge::setSelectedPath("/firmware/explicit.bin");
    assert(api->selected_path(selected, sizeof(selected)));
    assert(std::string(selected) == "/firmware/explicit.bin");
    assert(api->validate() == T5_SD_FIRMWARE_OK);
    assert(sd_firmware_opened_path == "/firmware/explicit.bin");
    assert(validator_path == "/firmware/explicit.bin");

    NativeSdFirmwareBridge::clearSelectedPath();
    active_source_available = false;
    assert(api->validate() == T5_SD_FIRMWARE_UNAVAILABLE);
    assert(!api->selected_path(selected, sizeof(selected)));
    active_source_available = true;

    sd_firmware_file_open_succeeds = false;
    assert(api->validate() == T5_SD_FIRMWARE_FILE_OPEN_FAILED);
    sd_firmware_file_open_succeeds = true;

    sd_firmware_partition_available = false;
    assert(api->validate() == T5_SD_FIRMWARE_INVALID);
    sd_firmware_partition_available = true;

    sd_firmware_partition_size = 64;
    const int calls_before_oversize = validator_calls;
    assert(api->validate() == T5_SD_FIRMWARE_TOO_LARGE);
    assert(validator_calls == calls_before_oversize);
    sd_firmware_partition_size = 4096;

    validator_result = firmware_flash::Result::BAD_MAGIC;
    assert(api->validate() == T5_SD_FIRMWARE_INVALID);
    validator_result = firmware_flash::Result::OK;
    assert(api->validate() == T5_SD_FIRMWARE_OK); /* Retry after a transient validator error. */
    assert(validator_path == active_source_path);

    install_result = firmware_flash::Result::WRITE_FAIL;
    assert(api->install(progress_callback, nullptr) == T5_SD_FIRMWARE_WRITE_FAILED);
    assert(install_path == active_source_path && install_calls == 1);
    install_result = firmware_flash::Result::OK;
    assert(api->install(progress_callback, nullptr) == T5_SD_FIRMWARE_OK);
    assert(install_path == active_source_path);
    assert(install_calls == 2 && progress_calls == 2);
    assert(api->image_size() == 128 && api->written_size() == 64);
    api->restart_after_update();
    assert(restart_calls == 1);

    app_api_available = false;
    assert(t5_sd_firmware_get_api(T5_SD_FIRMWARE_API_VERSION) == nullptr);
    assert(api->image_size() == 0 && api->written_size() == 0);
    return 0;
}
