#pragma once
constexpr int WIFI_MODE_NULL = 0;
struct TestWiFi { int mode = WIFI_MODE_NULL; int getMode() const { return mode; } };
extern TestWiFi WiFi;
