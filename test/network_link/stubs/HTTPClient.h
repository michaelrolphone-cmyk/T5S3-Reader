#pragma once
#include "NetworkTestSupport.h"
#include "NetworkClientSecure.h"
struct Cookie { std::string name, value, domain, path; };
using CookieJar = std::vector<Cookie>;
constexpr int HTTPC_STRICT_FOLLOW_REDIRECTS = 1;
class HTTPClient {
 public:
  void setCookieJar(CookieJar*) {}
  bool begin(NetworkClientSecure&, const char*) { Fixture::events.push_back("http.begin"); return Fixture::httpBegin; }
  void setTimeout(uint16_t) {}
  void setFollowRedirects(int) {}
  void setReuse(bool) {}
  void addHeader(const char*, const char*) {}
  int sendRequest(const char*, uint8_t*, size_t) { return GET(); }
  int GET() {
    Fixture::events.push_back("http.perform");
    if (Fixture::duringRequest) Fixture::duringRequest();
    return Fixture::httpResult;
  }
  int writeToStream(Stream* stream) { return static_cast<int>(stream->write(reinterpret_cast<const uint8_t*>("ok"), 2)); }
  void end() { Fixture::events.push_back("http.end"); }
};
