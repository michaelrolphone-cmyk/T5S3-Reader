#pragma once

#include <string>
#include <vector>

// Fetches one release-level app catalog and returns each embedded app manifest
// as compact JSON. The caller remains responsible for normal manifest validation
// and matching each manifest to a release ELF asset.
bool fetchAppCatalogIndex(const std::string& url, std::vector<std::string>& manifests);
