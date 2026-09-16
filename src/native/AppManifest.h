#pragma once
#include <T5AppApi.h>
#include "runtime/capabilities/AppCapabilityRequirements.h"
#include <string>

// Legacy callers may omit requirements. Declarations are validated even if
// they do not request the optional parsed result.
bool parseAppManifest(const std::string& json, t5_app_manifest_t& out,
                      std::string* appVersion = nullptr, bool requireAppVersion = false,
                      RuntimeDevices::AppCapabilityRequirements* requirements = nullptr);
bool readAppManifest(const char* storagePath, t5_app_manifest_t& out,
                     std::string* appVersion = nullptr, bool requireAppVersion = false,
                     RuntimeDevices::AppCapabilityRequirements* requirements = nullptr);
