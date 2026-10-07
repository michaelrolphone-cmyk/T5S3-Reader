// Isolated lab idle firmware. No filesystem, radio, camera, display or GPIO setup.
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

static uint8_t chipMac[6];
static uint32_t sequence = 0;
static uint32_t previous = 0;

void setup() {
  Serial.begin(115200);
  esp_efuse_mac_get_default(chipMac);
}

void loop() {
  const uint32_t now = millis();
  if (now - previous >= 2000) {
    previous = now;
    const esp_partition_t* app = esp_ota_get_running_partition();
    Serial.printf("RTE_HEARTBEAT version=%s target=%s mac=%02x:%02x:%02x:%02x:%02x:%02x sequence=%lu uptime_ms=%lu heap=%u app=0x%lx\n",
      RISCRTE_HEARTBEAT_VERSION, RISCRTE_HEARTBEAT_TARGET,
      chipMac[0], chipMac[1], chipMac[2], chipMac[3], chipMac[4], chipMac[5],
      static_cast<unsigned long>(++sequence), static_cast<unsigned long>(now),
      ESP.getFreeHeap(), static_cast<unsigned long>(app ? app->address : 0));
  }
  delay(20);
}
