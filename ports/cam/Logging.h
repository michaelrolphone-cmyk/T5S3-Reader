#pragma once
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
// Reserved boot console only; bounded one-way messages, no command shell.
inline void runtimePortLog(const char* origin,const char* format,...) {
  char text[256]; va_list args; va_start(args,format);
  vsnprintf(text,sizeof(text),format,args); va_end(args);
  Serial.printf("RUNTIME %.16s %.255s\n",origin,text);
}
#define LOG_ERR(origin, format, ...) runtimePortLog(origin,format,##__VA_ARGS__)
#define LOG_INF(origin, format, ...) runtimePortLog(origin,format,##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) do {} while(0)
