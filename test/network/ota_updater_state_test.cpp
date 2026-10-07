#include <Arduino.h>
#define CROSSPOINT_VERSION "1.3.55"
#include "../../src/network/OtaUpdater.cpp"

#include <cassert>
#include <cstring>

int main() {
  RuntimeNetwork::gReady = true;
  ReleaseJsonParser::hasTag = true;
  ReleaseJsonParser::hasFirmware = true;
  ReleaseJsonParser::version = "v9.9.9";
  ReleaseJsonParser::url = "https://example.invalid/firmware.bin";
  ReleaseJsonParser::size = 4096;

  OtaUpdater updater;
  assert(updater.checkForUpdate() == OtaUpdater::OK);
  assert(updater.isUpdateNewer());
  assert(updater.getLatestVersion() == "v9.9.9");
  assert(updater.getOtaSize() == 4096);
  assert(updater.getTotalSize() == 4096);
  ota_https_ota_read_bytes = 512;
  assert(updater.installUpdate() == OtaUpdater::OK);
  assert(updater.getProcessedSize() == 512);

  // An HTTP transport error following a successful check cannot retain A.
  ota_http_perform_result = 1;
  assert(updater.checkForUpdate() == OtaUpdater::HTTP_ERROR);
  assert(!updater.isUpdateNewer());
  assert(updater.getLatestVersion().empty());
  assert(updater.getOtaSize() == 0);
  assert(updater.getTotalSize() == 0);
  ota_http_perform_result = ESP_OK;
  assert(updater.checkForUpdate() == OtaUpdater::OK);

  // A later network failure must invalidate the old release before returning.
  RuntimeNetwork::gReady = false;
  assert(updater.checkForUpdate() == OtaUpdater::HTTP_ERROR);
  assert(!updater.isUpdateNewer());
  assert(updater.getLatestVersion().empty());
  assert(updater.getOtaSize() == 0);
  assert(updater.getProcessedSize() == 0);
  assert(updater.getTotalSize() == 0);
  const int beginCount = esp_https_ota_begin_count;
  assert(updater.installUpdate() == OtaUpdater::UPDATE_OLDER_ERROR);
  assert(esp_https_ota_begin_count == beginCount);

  // A successful retry republishes only the new discovery.
  RuntimeNetwork::gReady = true;
  assert(updater.checkForUpdate() == OtaUpdater::OK);
  assert(updater.isUpdateNewer());
  assert(updater.getLatestVersion() == "v9.9.9");

  // Legacy fallback with a valid release tag but no board image means no update.
  ReleaseJsonParser::hasFirmware = false;
  assert(updater.checkForUpdate() == OtaUpdater::NO_UPDATE);
  assert(!updater.isUpdateNewer());
  assert(updater.getLatestVersion().empty());
  assert(updater.getOtaSize() == 0);
  assert(updater.getTotalSize() == 0);

  // A malformed index must also invalidate a prior successful legacy result.
  ReleaseJsonParser::hasFirmware = true;
  assert(updater.checkForUpdate() == OtaUpdater::OK);
  HttpDownloader::gFetchUrlReturns = true;
  gDeserializeFails = true;
  assert(updater.checkForUpdate() == OtaUpdater::JSON_PARSE_ERROR);
  assert(!updater.isUpdateNewer());
  assert(updater.getLatestVersion().empty());
  assert(updater.getOtaSize() == 0);
  assert(updater.getTotalSize() == 0);
  HttpDownloader::gFetchUrlReturns = false;
  gDeserializeFails = false;

  // A malformed release response also leaves no previously discovered target.
  assert(updater.checkForUpdate() == OtaUpdater::OK);
  ReleaseJsonParser::hasTag = false;
  assert(updater.checkForUpdate() == OtaUpdater::JSON_PARSE_ERROR);
  assert(!updater.isUpdateNewer());
  assert(updater.getLatestVersion().empty());
  assert(updater.getOtaSize() == 0);
  assert(updater.getTotalSize() == 0);
  return 0;
}
