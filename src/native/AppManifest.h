#pragma once
#include <T5AppApi.h>
#include <string>
bool parseAppManifest(const std::string& json, t5_app_manifest_t& out,
                      std::string* appVersion = nullptr, bool requireAppVersion = false);
bool readAppManifest(const char* storagePath, t5_app_manifest_t& out,
                     std::string* appVersion = nullptr, bool requireAppVersion = false);
