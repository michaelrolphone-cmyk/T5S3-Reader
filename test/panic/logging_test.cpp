#include "Logging.h"
#include <esp_log.h>
#include <cassert>
#include <cstdio>
#include <cstring>
static int forwarded = 0;
static int sink(const char*, va_list) { ++forwarded; return 7; }
static vprintf_like_t current = sink;
vprintf_like_t esp_log_set_vprintf(vprintf_like_t next) { auto prev=current; current=next; return prev; }
static void sdk(const char *format, ...) { va_list ap; va_start(ap, format); current(format, ap); va_end(ap); }
int main() {
  clearLastLogs();
  installSdkLogCapture(); installSdkLogCapture();
  sdk("Gameboy HID stage %d\n", 42);
  assert(forwarded == 1 && getLastLogs().find("stage 42") != std::string::npos);
  preserveLastLogs(true);
  for (int i=0;i<30;++i) { sdk("boot %d\n",i); logPrintf("INF","SD","SD card detected\n"); }
  assert(getLastLogs().find("stage 42") != std::string::npos);
  assert(getLastLogs().find("SD card") == std::string::npos && forwarded == 31);
  preserveLastLogs(false); clearLastLogs();
  sdk("%01000d", 1);
  assert(getLastLogs().size() == 255); // bounded SDK formatting
  clearLastLogs(); sdk("next launch\n");
  assert(getLastLogs() == "next launch\n");
  puts("SDK logging: capture, forwarding, reboot preservation, truncation and resume passed");
}
