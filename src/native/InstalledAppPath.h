#pragma once

#include <string>

#include <T5AppApi.h>

// Resolve an application ELF basename to a verified SD application.
// Managed /Apps/<id>/<artifact> packages take precedence over legacy loose
// /Apps/<artifact> pairs. The result is an absolute /sd path suitable for
// runNativeApp().
bool resolveInstalledAppPath(const char* artifact, std::string& sdPath,
                             t5_app_manifest_t* manifest = nullptr);
