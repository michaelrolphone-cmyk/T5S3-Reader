#include "native/AppReleaseAssetRules.h"
#include <cassert>
#include <iostream>

int main() {
  using NativeAppReleaseRules::appElfAssetName;
  assert(appElfAssetName("app_store.elf"));
  assert(appElfAssetName("driver_manager.elf"));
  assert(appElfAssetName("ESP_ROM_FLASHER.ELF"));
  assert(!appElfAssetName("gps-nmea-1.0.0.t5driver.elf"));
  assert(!appElfAssetName("usb-cdc-acm-0.1.0.t5driver.elf"));
  assert(!appElfAssetName("usb-ftdi--driver.elf"));
  assert(!appElfAssetName("board-power-t5s3-v2--driver.ELF"));
  assert(!appElfAssetName("../app.elf"));
  assert(!appElfAssetName("folder/app.elf"));
  assert(!appElfAssetName("app.json"));
  std::cout << "App Store release asset classification passed\n";
  return 0;
}
