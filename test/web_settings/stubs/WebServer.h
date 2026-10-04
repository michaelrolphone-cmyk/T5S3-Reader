#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "Arduino.h"
#define CONTENT_LENGTH_UNKNOWN 0
class WebServer {
 public:
  struct Socket {
    WebServer* server;
    explicit Socket(WebServer* owner) : server(owner) {}
    ~Socket() { server->alive = false; ++server->stops; }
  };
  struct Client {
    std::shared_ptr<Socket> socket;
    bool connected() const { return socket && socket->server->alive; }
    void stop() { socket.reset(); }
  };
  std::string body;
  std::vector<size_t> chunks;
  bool alive = true;
  unsigned terminators = 0, stops = 0, failAfter = 0, finalizationAttempts = 0;
  uint32_t sendDelay = 0;
  size_t largestChunk = 0;
  int status = 0;
  WebServer() : _currentClient{std::make_shared<Socket>(this)} {}
  WebServer(const WebServer&) = delete;
  WebServer& operator=(const WebServer&) = delete;
  void reset() {
    _currentClient.stop();
    body.clear(); chunks.clear(); alive = true;
    terminators = stops = failAfter = finalizationAttempts = sendDelay = 0;
    largestChunk = 0; status = 0; _chunked = false;
    _currentClient.socket = std::make_shared<Socket>(this);
  }
  void setContentLength(size_t) {}
  Client client() { return _currentClient; }
  void finalizeResponse() {
    if (_chunked) { ++finalizationAttempts; sendContent(""); }
  }
  void send(int code, const char* type, const char* text) {
    assert(std::string(type) == "application/json");
    status = code; body = text; _chunked = true;
  }
  void sendContent(const char* text) { sendContent(text, std::strlen(text)); }
  void sendContent(const char* text, size_t length) {
    if (!_currentClient.connected()) return;
    if (length == 0) { ++terminators; _chunked = false; return; }
    assert(terminators == 0);
    body.append(text, length);
    chunks.push_back(length);
    largestChunk = std::max(largestChunk, length);
    testMillis += sendDelay;
    if (failAfter && chunks.size() == failAfter) alive = false;
  }
 protected:
  bool _chunked = false;
  Client _currentClient;
};
