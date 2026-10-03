#pragma once
#include <cstdint>
enum esp_sntp_operatingmode_t { ESP_SNTP_OPMODE_POLL };
enum sntp_sync_status_t { SNTP_SYNC_STATUS_RESET, SNTP_SYNC_STATUS_COMPLETED };
bool esp_sntp_enabled();
void esp_sntp_stop();
void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t mode);
void esp_sntp_setservername(uint8_t index, char* server);
void esp_sntp_init();
sntp_sync_status_t sntp_get_sync_status();
constexpr uint32_t portTICK_PERIOD_MS = 1;
