#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include "Arduino.h"
#define CONTENT_LENGTH_UNKNOWN 0
class WebServer {
 public:
  struct Client {
    WebServer* server;
    bool connected() const { return server->alive; }
    void stop() { server->alive = false; ++server->stops; }
  };
  std::string body;
  std::vector<size_t> chunks;
  bool alive = true;
  unsigned terminators = 0, stops = 0, failAfter = 0;
  uint32_t sendDelay = 0;
  size_t largestChunk = 0;
  int status = 0;
  void setContentLength(size_t) {}
  Client client() { return {this}; }
  void send(int code, const char* type, const char* text) {
    assert(std::string(type) == "application/json");
    status = code;
    body = text;
  }
  void sendContent(const char* text) { sendContent(text, std::strlen(text)); }
  void sendContent(const char* text, size_t length) {
    assert(alive);
    if (length == 0) { ++terminators; return; }
    assert(terminators == 0);
    body.append(text, length);
    chunks.push_back(length);
    largestChunk = std::max(largestChunk, length);
    testMillis += sendDelay;
    if (failAfter && chunks.size() == failAfter) alive = false;
  }
};
