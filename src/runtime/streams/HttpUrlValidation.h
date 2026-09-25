#pragma once

#include <stddef.h>

namespace RuntimeHttpUrl {

enum class Status {
  Valid,
  Null,
  TooLong,
  UnsupportedScheme,
};

// capacity is the maximum accepted C-string length plus its required NUL.
// The returned length is capped at capacity when no NUL is found.
inline Status validate(const char* url, size_t capacity, size_t* length = nullptr) {
  if (length) *length = 0;
  if (!url) return Status::Null;

  size_t size = 0;
  while (size < capacity && url[size] != '\0') ++size;
  if (length) *length = size;
  if (size == capacity) return Status::TooLong;

  const bool https = size >= 8 && url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
      url[3] == 'p' && url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/';
  const bool http = size >= 7 && url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
      url[3] == 'p' && url[4] == ':' && url[5] == '/' && url[6] == '/';
  return https || http ? Status::Valid : Status::UnsupportedScheme;
}

inline const char* statusName(Status status) {
  switch (status) {
    case Status::Valid: return "valid";
    case Status::Null: return "null";
    case Status::TooLong: return "too-long";
    case Status::UnsupportedScheme: return "unsupported-scheme";
  }
  return "unknown";
}

}  // namespace RuntimeHttpUrl
