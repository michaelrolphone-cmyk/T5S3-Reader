#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
inline std::vector<std::string> nativeDiagnosticLogs;
inline void nativeDiagnosticLog(const char*, const char* format, ...) {
  char text[384]{};
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  nativeDiagnosticLogs.emplace_back(text);
}
inline bool nativeDiagnosticContains(const char* text) {
  for (const auto& message : nativeDiagnosticLogs)
    if (message.find(text) != std::string::npos) return true;
  return false;
}
#define LOG_INF(...) nativeDiagnosticLog(__VA_ARGS__)
#define LOG_DBG(...) nativeDiagnosticLog(__VA_ARGS__)
#define LOG_ERR(...) nativeDiagnosticLog(__VA_ARGS__)
