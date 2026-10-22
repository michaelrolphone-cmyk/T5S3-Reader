#pragma once
#include "NetworkTestSupport.h"
using wifi_ps_type_t = int;
constexpr int WIFI_PS_MIN_MODEM = 1, WIFI_PS_NONE = 0;
inline int esp_wifi_get_ps(int* out) { ++Fixture::powerReads; *out = WIFI_PS_MIN_MODEM; return ESP_OK; }
inline int esp_wifi_set_ps(int) { ++Fixture::powerWrites; return ESP_OK; }
