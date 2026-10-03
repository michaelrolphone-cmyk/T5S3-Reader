#include "FlashModuleStore.h"
#if defined(BOARD_XTEINK_X4_PRO)
#include <esp_littlefs.h>
#include <Logging.h>
bool mountFlashModuleStore() {
  // Use the existing data partition, with no layout change and no format-on-
  // failure. Its image is separately provisioned; never linked into firmware.
  esp_vfs_littlefs_conf_t config{};
  config.base_path="/bootfs";
  config.partition_label="spiffs";
  config.format_if_mount_failed=false;
  config.read_only=true;
  config.grow_on_mount=false;
  const bool mounted=esp_vfs_littlefs_register(&config)==ESP_OK;
  if(!mounted) LOG_ERR("BOOTFS","External module store unavailable; provision required (no format attempted)");
  return mounted;
}
#else
bool mountFlashModuleStore() { return false; }
#endif
