#include "OtaUpdater.h"

#include <Board.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_idf_version.h>
#include <esp_wifi.h>

#include "GithubTlsCerts.h"
#include "runtime/network/NetworkService.h"

namespace {
constexpr char latestReleaseUrl[] =
    "https://api.github.com/repos/michaelrolphone-cmyk/T5S3-Reader/releases/latest";

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  return esp_http_client_set_header(http_client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
}

size_t totalBytesReceived = 0;

esp_err_t event_handler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  totalBytesReceived += event->data_len;
  LOG_DBG("OTA", "HTTP chunk: %d bytes (total: %zu)", event->data_len, totalBytesReceived);
  auto* parser = static_cast<ReleaseJsonParser*>(event->user_data);
  parser->feed(static_cast<const char*>(event->data), event->data_len);
  return ESP_OK;
}

const char* skipVersionPrefix(const char* version) {
  if (version == nullptr) return "";
  if (version[0] == 'v' || version[0] == 'V') return version + 1;
  return version;
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  // A direct ELF launch does not pass through Settings' Wi-Fi picker. Do not
  // invoke ESP-IDF HTTP until an initialized interface has a usable IP; lwIP
  // otherwise can assert at tcpip_send_msg_wait_sem with "Invalid mbox".
  if (!RuntimeNetwork::ready()) {
    LOG_ERR("OTA", "Update check refused: network is not ready");
    return HTTP_ERROR;
  }

  esp_err_t esp_err;
  ReleaseJsonParser releaseParser(Board::id());

  // Do not use crt_bundle_attach. Arduino-ESP32 2.x does not link the Mozilla
  // bundle into this firmware, so attach fails and mbedtls returns -0x7680.
  esp_http_client_config_t client_config = {
      .url = latestReleaseUrl,
      .cert_pem = kGithubTlsRoots,
      .event_handler = event_handler,
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .user_data = &releaseParser,
      .skip_cert_common_name_check = true,
      .keep_alive_enable = true,
  };

  totalBytesReceived = 0;
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    LOG_ERR("OTA", "HTTP Client Handle Failed");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_set_header(client_handle, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_set_header Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_perform(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_perform Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return HTTP_ERROR;
  }

  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_cleanup Failed : %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_DBG("OTA", "Response received: %zu bytes total", totalBytesReceived);
  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_ERR("OTA", "No firmware-%s.bin asset found", Board::id());
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  int currentMajor = 0, currentMinor = 0, currentPatch = 0;
  int latestMajor = 0, latestMinor = 0, latestPatch = 0;

  const char* currentVersion = skipVersionPrefix(CROSSPOINT_VERSION);
  const char* latest = skipVersionPrefix(latestVersion.c_str());

  sscanf(latest, "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  sscanf(currentVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

  if (latestMajor != currentMajor) return latestMajor > currentMajor;
  if (latestMinor != currentMinor) return latestMinor > currentMinor;
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  if (strstr(CROSSPOINT_VERSION, "-rc") != nullptr) {
    return true;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }
  // An OTA image transfer must not start after Wi-Fi teardown or before the
  // connection acquires an IP. Return an error instead of entering lwIP with
  // an invalid TCP/IP mailbox.
  if (!RuntimeNetwork::ready()) {
    LOG_ERR("OTA", "Firmware install refused: network is not ready");
    return HTTP_ERROR;
  }

  esp_https_ota_handle_t ota_handle = NULL;
  esp_err_t esp_err;

  esp_http_client_config_t client_config = {
      .url = otaUrl.c_str(),
      .cert_pem = kGithubTlsRoots,
      .timeout_ms = 15000,
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .skip_cert_common_name_check = true,
      .keep_alive_enable = true,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &client_config,
      .http_client_init_cb = http_client_set_header_cb,
  };

  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_err = esp_https_ota_begin(&ota_config, &ota_handle);
  if (esp_err != ESP_OK) {
    LOG_DBG("OTA", "HTTP OTA Begin Failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  do {
    esp_err = esp_https_ota_perform(ota_handle);
    processedSize = esp_https_ota_get_image_len_read(ota_handle);
    if (onProgress) onProgress(ctx);
    delay(100);
  } while (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_perform Failed: %s", esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return HTTP_ERROR;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    LOG_ERR("OTA", "esp_https_ota_is_complete_data_received Failed: %s", esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_https_ota_finish(ota_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_finish Failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
