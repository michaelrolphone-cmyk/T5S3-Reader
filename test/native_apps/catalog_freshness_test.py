#!/usr/bin/env python3
"""Run the real metadata request path with a simulated upstream cache/transport.

No network or device claims: extract HttpDownloader::fetchUrl(Stream&) verbatim,
compile with the production URL policy and substitute only platform/HTTP I/O.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/HttpDownloader.cpp").read_text()

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "ReleaseCatalogRequest.h"
#include "HttpClientBudget.h"
using String = std::string;
#define CROSSPOINT_VERSION "test"
#define HTTP_CODE_OK 200
#define HTTPC_STRICT_FOLLOW_REDIRECTS 1
static void logLine(const char*,const char*,...) {}
#define LOG_DBG logLine
#define LOG_INF logLine
#define LOG_ERR logLine
static bool networkReady=true, nativeInvocation=false;
static unsigned nonceCalls=0, requests=0, streamDelegations=0, ends=0;
static int responseStatus=200, writeStatus=0;
static uint32_t nowMs=0, transferDelay=0;
static uint32_t millis() { return nowMs; }
static void delay(unsigned ms) { nowMs+=ms; }
static std::string wireUrl, delegatedUrl;
static std::map<std::string,std::string> headers;
static std::vector<std::string> collected;
static uint32_t esp_random() { return ++nonceCalls; }
static bool waitForNetworkReady() { return networkReady; }
static void logHttpMemory(const char*) {}
static int streamHooks() { return 0; }
namespace UrlUtils { static bool isHttpsUrl(const std::string& url) { return url.find("https://")==0; } }
namespace base64 { static String encode(const char*) { return "encoded"; } }
class Stream {
 public:
  virtual size_t write(const uint8_t*,size_t)=0;
  virtual ~Stream()=default;
};
class TextStream : public Stream {
 public:
  std::string value;
  size_t write(const uint8_t* data,size_t size) override { value.append(reinterpret_cast<const char*>(data),size);return size; }
};
class CrossPointHttpClient {
 public:
  virtual ~CrossPointHttpClient()=default;
  virtual int connect(const char*,uint16_t,int32_t) { return 1; }
  virtual int available() { return 0; }
  virtual uint8_t connected() { return 1; }
  virtual int peek() { return -1; }
  virtual int read() { return -1; }
  virtual int read(uint8_t*,size_t) { return -1; }
  virtual size_t readBytes(char*,size_t) { return 0; }
  virtual size_t readBytes(uint8_t*,size_t) { return 0; }
  virtual size_t write(uint8_t) { return 1; }
  virtual size_t write(const uint8_t*,size_t n) { return n; }
  virtual void stop() {}
};
class CrossPointHttpClientSecure : public CrossPointHttpClient { public: void setInsecure() {} void setHandshakeTimeout(unsigned seconds) { assert(seconds==15); } };
class HTTPClient {
 public:
  void begin(CrossPointHttpClient&,const char* url) { wireUrl=url;headers.clear();collected.clear(); }
  void setConnectTimeout(int ms) { assert(ms==5000); }
  void setTimeout(unsigned ms) { assert(ms==5000); }
  void setFollowRedirects(int policy) { assert(policy==HTTPC_STRICT_FOLLOW_REDIRECTS); }
  void addHeader(const char* key,const String& value) { headers[key]=value; }
  void collectHeaders(const char** names,size_t count) { collected.assign(names,names+count); }
  String header(const char* key) { return !std::strcmp(key,"Age")?"0":"test"; }
  int GET() { ++requests;return responseStatus; }
  int writeToStream(Stream* stream) {
    if(writeStatus<0) return writeStatus;
    nowMs+=transferDelay;
    // A URI-keyed shared cache still has the old response at the bare URL.
    const std::string content=wireUrl.find("_rte_refresh=")!=std::string::npos?"current":"cached";
    return static_cast<int>(stream->write(reinterpret_cast<const uint8_t*>(content.data()),content.size()));
  }
  void end() { ++ends; }
};
class HttpDownloader {
 public:
  static bool fetchUrl(const std::string&,Stream&,const std::string& = "",const std::string& = "");
};
static const int* invocationStreams(const std::string& user,const std::string& password) {
  static int api;
  return nativeInvocation && user.empty() && password.empty()?&api:nullptr;
}
namespace RuntimeHttpStreams {
  enum class Result { Ok, Http };
  static Result fetch(const int*,const char* url,int,bool (*receive)(void*,const uint8_t*,uint32_t),void* context) {
    ++streamDelegations;delegatedUrl=url;
    class ForwardStream : public Stream {
     public:
      decltype(receive) sink;void* ctx;
      ForwardStream(decltype(receive) fn,void* p):sink(fn),ctx(p) {}
      size_t write(const uint8_t* bytes,size_t size) override { return sink(ctx,bytes,static_cast<uint32_t>(size))?size:0; }
    } stream(receive,context);
    nativeInvocation=false;
    const bool ok=HttpDownloader::fetchUrl(url,stream,"","");
    nativeInvocation=true;
    return ok?Result::Ok:Result::Http;
  }
}
'''

TESTS = r'''
int main() {
  const std::string index="https://raw.githubusercontent.com/michaelrolphone-cmyk/T5S3-Reader/release-index/release-index.json";
  const std::string latest="https://api.github.com/repos/michaelrolphone-cmyk/T5S3-Reader/releases/latest";
  const std::string release="https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/";
  const std::vector<std::string> mutableUrls={index,latest,release+"latest/download/app-catalog.json",release+"latest/download/driver-catalog.json",release+"latest/download/package-catalog.json"};
  const std::vector<std::string> immutableUrls={
      release+"download/app-model_viewer-v1.2.0/model_viewer.json",
      release+"download/app-model_viewer-v1.2.0/model_viewer.elf",
      release+"download/firmware-v1.3.22/firmware-t5s3-pro.bin",
      release+"download/driver-test-v1.0.0/driver-test-1.0.0-xtensa-esp32s3.rte.zip",
      release+"download/v1.0.0/package-catalog.json",
      "https://raw.githubusercontent.com/michaelrolphone-cmyk/T5S3-Reader/5258a9f/release-index.json",
      "https://raw.githubusercontent.com.attacker.test/michaelrolphone-cmyk/T5S3-Reader/release-index/release-index.json",
      index+".backup", "https://example.test/catalog.json?signature=abc", ""};
  for(const auto& url:mutableUrls) {
    assert(ReleaseCatalogRequest::isMutableCatalog(url));
    assert(ReleaseCatalogRequest::freshUrl(url,0,1)==url+"?_rte_refresh=0000000000000001");
    assert(ReleaseCatalogRequest::freshUrl(url+"?x=1#top",0xffffffffu,0xffffffffu)==
        url+"?x=1&_rte_refresh=ffffffffffffffff#top");
    assert(ReleaseCatalogRequest::freshUrl(url+"?#top",0,0)==url+"?_rte_refresh=0000000000000000#top");
  }
  for(const auto& url:immutableUrls) {
    assert(!ReleaseCatalogRequest::isMutableCatalog(url));
    assert(ReleaseCatalogRequest::freshUrl(url,1,2)==url);
  }
  std::set<std::string> requestsSeen;
  for(bool native:{false,true}) for(const auto& url:mutableUrls) for(unsigned repeat=0;repeat<3;++repeat) {
    nativeInvocation=native;TextStream result;
    const unsigned randomBefore=nonceCalls,requestsBefore=requests,delegationsBefore=streamDelegations;
    assert(HttpDownloader::fetchUrl(url,result,"",""));
    assert(result.value=="current" && requests==requestsBefore+1 && nonceCalls==randomBefore+2);
    assert(requestsSeen.insert(wireUrl).second);
    assert(wireUrl.find("_rte_refresh=")==wireUrl.rfind("_rte_refresh="));
    assert(headers["Cache-Control"]=="no-cache, no-store, max-age=0" && headers["Pragma"]=="no-cache");
    assert((collected==std::vector<std::string>{"Age","ETag","X-Cache","Cache-Control"}));
    assert(streamDelegations==delegationsBefore+(native?1u:0u));
    if(native) assert(delegatedUrl==url); // nonce belongs to final transport, once.
  }
  for(bool native:{false,true}) for(const auto& url:immutableUrls) {
    nativeInvocation=native;TextStream result;const unsigned randomBefore=nonceCalls;
    assert(HttpDownloader::fetchUrl(url,result,"",""));
    assert(wireUrl==url && nonceCalls==randomBefore && headers.count("Cache-Control")==0 && collected.empty());
  }
  nativeInvocation=false;TextStream authenticated;
  const unsigned randomBefore=nonceCalls;
  assert(HttpDownloader::fetchUrl(index,authenticated,"user","password"));
  assert(wireUrl==index && headers.count("Authorization")==1 && nonceCalls==randomBefore);
  responseStatus=503;TextStream failed;
  assert(!HttpDownloader::fetchUrl(index,failed,"","") && failed.value.empty());
  responseStatus=200;writeStatus=-1;
  assert(!HttpDownloader::fetchUrl(index,failed,"","") && failed.value.empty());
  writeStatus=0;transferDelay=300000;TextStream late;
  assert(!HttpDownloader::fetchUrl(index,late,"","") && !late.value.empty());
  transferDelay=0;networkReady=false;const unsigned count=requests;
  assert(!HttpDownloader::fetchUrl(index,failed,"","") && requests==count);
  assert(ends==requests);
  std::puts("catalog: unique URI, revalidation, final-transport native/firmware routing, immutable URLs, diagnostics and failures PASS");
}
'''


class CatalogFreshnessTests(unittest.TestCase):
    def test_actual_metadata_transport(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler)
        start = SOURCE.index("bool HttpDownloader::fetchUrl(const std::string& url, Stream&")
        end = SOURCE.index("bool HttpDownloader::fetchUrl(const std::string& url, std::string&", start)
        code = HARNESS + SOURCE[start:end] + TESTS
        with tempfile.TemporaryDirectory(prefix="catalog-freshness-") as temp:
            source, binary = Path(temp) / "test.cpp", Path(temp) / "test"
            source.write_text(code)
            command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(ROOT / "src/network"), str(source), "-o", str(binary)]
            if os.environ.get("MV_SANITIZE") == "1":
                command[2:2] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)

    def test_binary_install_path_unchanged(self):
        body = SOURCE[SOURCE.index("HttpDownloader::DownloadError HttpDownloader::downloadToFile"):]
        self.assertNotIn("freshUrl", body)
        self.assertNotIn("requestRevalidation", body)
        self.assertIn("RuntimeHttpStreams::download(streams, url.c_str()", body)


if __name__ == "__main__":
    unittest.main()
