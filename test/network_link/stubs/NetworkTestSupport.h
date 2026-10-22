#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace Fixture {
extern std::vector<std::string> events;
extern bool appActive, httpBegin, httpInit, savedConnection;
extern int httpResult, powerReads, powerWrites, reconnects;
extern void (*duringRequest)();
}
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = 0x103, ESP_ERR_NO_MEM = 0x101;
class Stream {
 public:
  virtual ~Stream() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
};
