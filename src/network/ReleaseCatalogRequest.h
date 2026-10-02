#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Only moving catalog pointers get a unique request URL. Versioned manifests,
// ELF/firmware assets, authenticated URLs and unrelated metadata keep their
// original URLs and caching behavior. This runs at the physical HTTP layer,
// after native-stream delegation, so every consumer follows the same policy.
namespace ReleaseCatalogRequest {
inline bool isMutableCatalog(const std::string& url) {
  const size_t end = url.find_first_of("?#");
  const size_t length = end == std::string::npos ? url.size() : end;
  static const char* const urls[] = {
      "https://raw.githubusercontent.com/michaelrolphone-cmyk/T5S3-Reader/release-index/release-index.json",
      "https://api.github.com/repos/michaelrolphone-cmyk/T5S3-Reader/releases/latest",
      "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/app-catalog.json",
      "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/driver-catalog.json",
      "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/package-catalog.json",
  };
  for (const char* candidate : urls)
    if (url.compare(0, length, candidate) == 0) return true;
  return false;
}

inline std::string freshUrl(const std::string& url, uint32_t nonceHigh, uint32_t nonceLow) {
  if (!isMutableCatalog(url)) return url;
  // A random 64-bit cache key does not depend on RTC/NTP, boot uptime, the
  // installed firmware version or any device identifier. Generate per request.
  char token[32];
  std::snprintf(token, sizeof(token), "_rte_refresh=%08lx%08lx",
                static_cast<unsigned long>(nonceHigh), static_cast<unsigned long>(nonceLow));
  const size_t fragment = url.find('#');
  const size_t end = fragment == std::string::npos ? url.size() : fragment;
  std::string result = url.substr(0, end);
  const size_t query = result.find('?');
  if (query == std::string::npos) result += '?';
  else if (result.back() != '?' && result.back() != '&') result += '&';
  result += token;
  if (fragment != std::string::npos) result += url.substr(fragment);
  return result;
}

template <typename Client>
inline void requestRevalidation(Client& http) {
  http.addHeader("Cache-Control", "no-cache, no-store, max-age=0");
  http.addHeader("Pragma", "no-cache");
}
}  // namespace ReleaseCatalogRequest
