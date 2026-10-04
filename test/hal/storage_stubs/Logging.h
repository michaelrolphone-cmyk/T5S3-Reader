#pragma once
#include <cstdio>
#include <string>
#include <vector>
namespace FakeStorageLog {
inline std::vector<std::string> lines;
inline bool contains(const char* text) {
  for (const auto& line : lines) if (line.find(text) != std::string::npos) return true;
  return false;
}
}
template <class... Args>
inline void fakeStorageLog(const char*, const char* format, Args... args) {
  char text[512]{};
  std::snprintf(text, sizeof(text), format, args...);
  if (FakeStorageLog::lines.size() < 256) FakeStorageLog::lines.emplace_back(text);
}
#define LOG_ERR(...) fakeStorageLog(__VA_ARGS__)
#define LOG_INF(...) fakeStorageLog(__VA_ARGS__)
#define LOG_DBG(...) fakeStorageLog(__VA_ARGS__)
