#pragma once
#include <cstdio>
#include <string>
#include <vector>

inline std::vector<std::string> ota_error_logs;
template <typename... Args>
inline void ota_log_error(const char* format, Args... args) {
  char message[256];
  std::snprintf(message, sizeof(message), format, args...);
  ota_error_logs.emplace_back(message);
}
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(tag, ...) ota_log_error(__VA_ARGS__)
#define LOG_INF(...) ((void)0)
