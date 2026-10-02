#pragma once
#include "esp_err.h"
#include <cstring>
#include <cstdint>
constexpr int ESP_MAC_WIFI_STA=0;
namespace Fake { inline bool identity=true; }
inline int esp_read_mac(uint8_t* p, int) {
 const uint8_t mac[]={0x28,0x84,0x85,0x4b,0x57,0x98};
 memcpy(p,mac,6); if(!Fake::identity) p[0]=0; return 0;
}
