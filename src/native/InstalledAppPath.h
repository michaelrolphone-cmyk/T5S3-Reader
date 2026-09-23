#pragma once

#include <string>

#include <T5AppApi.h>

// Resolve an ELF basename, as stored in /Apps/.home_apps, to a verified SD
// application. Managed packages take precedence over legacy loose ELF pairs.
// The result is an absolute /sd path suitable for runNativeApp().
bool resolveInstalledAppPath(const char* artifact, std::string& sdPath,
                             t5_app_manifest_t* manifest = nullptr);
