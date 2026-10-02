#!/usr/bin/env python3
"""Actual bounded downloader method + actual stream transfer/registry, fake SD/network readiness."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/network/HttpDownloader.cpp').read_text()
method = source[source.index('HttpDownloader::DownloadError HttpDownloader::downloadToFileBounded('):]
harness = r'''
#include <functional>
#include <limits>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_transfer_fixture
#include "test/streams/http_transfer_test.cpp"
#undef main
#pragma GCC diagnostic pop
using namespace RuntimeHttpStreams;
static bool nativeInvocation=true, ready=true, fileCloseOkay=true, wrongSize=false, hideExisting=false;
static uint32_t readinessMs=0;
static unsigned removals=0;
static uint32_t millis(){return Mock::active->now;}
constexpr uint32_t kNetworkReadyTimeoutMs=15000;
namespace RuntimeNetwork {
bool ensureSavedConnection(uint32_t limit){Mock::active->now+=std::min(readinessMs,limit);return ready;}
}
static const t5_stream_api_v1* invocationStreams(const std::string&,const std::string&){return nativeInvocation?&Mock::active->api:nullptr;}
static Hooks streamHooks(){return Mock::active->hooks();}
constexpr int O_RDONLY=0;
struct HalFile {
  bool open=false;
  bool isOpen(){return open;}
  bool isDirectory(){return false;}
  uint64_t fileSize64(){return Mock::active->staged.size()+(wrongSize?1:0);}
  bool close(){open=false;return fileCloseOkay;}
};
struct FakeStorage {
  bool exists(const char*){return !hideExisting && Mock::active->stageExists;}
  HalFile open(const char*,int){return {Mock::active->stageExists};}
  bool remove(const char*){++removals;Mock::active->stageExists=false;Mock::active->staged.clear();return true;}
} Storage;
class HttpDownloader {
 public:
  using ProgressCallback=std::function<void(size_t,size_t)>;
  enum DownloadError{OK,HTTP_ERROR,FILE_ERROR,ABORTED,STREAM_ERROR};
  static DownloadError downloadToFileBounded(const std::string&,const std::string&,uint64_t,uint32_t,ProgressCallback=nullptr);
};
'''
tests = r'''
static auto run(uint64_t bytes=100,uint32_t time=10000){return HttpDownloader::downloadToFileBounded("https://example.test/payload","/Apps/test.elf.part",bytes,time);}
int main(){
  {Mock m(100);assert(run()==HttpDownloader::OK);assert(m.staged==m.body&&!removals);assertReleased(m);}
  {Mock m(100);m.stageExists=true;m.staged={1,2,3};assert(run()==HttpDownloader::FILE_ERROR);assert(!removals&&m.staged.size()==3);}
  {Mock m(100);m.stageExists=true;m.staged={1,2,3};hideExisting=true;assert(run()==HttpDownloader::FILE_ERROR);hideExisting=false;assert(!removals&&m.staged.size()==3);}
  {Mock m(100);assert(run(50)==HttpDownloader::STREAM_ERROR);assert(removals==1&&!m.stageExists);assertReleased(m);}
  {Mock m(50);assert(run()==HttpDownloader::STREAM_ERROR);assert(removals==2&&!m.stageExists);assertReleased(m);}
  {Mock m(100);nativeInvocation=false;assert(run()==HttpDownloader::STREAM_ERROR);nativeInvocation=true;assert(!m.stageExists&&removals==2);}
  {Mock m(100);readinessMs=100;assert(run(100,100)==HttpDownloader::STREAM_ERROR);readinessMs=0;assert(!m.stageExists&&removals==2);}
  {Mock m(100);wrongSize=true;assert(run()==HttpDownloader::STREAM_ERROR);wrongSize=false;assert(removals==3&&!m.stageExists);assertReleased(m);}
  {Mock m(100);fileCloseOkay=false;assert(run()==HttpDownloader::STREAM_ERROR);fileCloseOkay=true;assert(removals==4&&!m.stageExists);assertReleased(m);}
  puts("Actual bounded downloader: exact length, context, setup deadline, close/size faults and owned-stage-only cleanup PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='rte-bounded-download-') as tmp:
    cpp, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
    cpp.write_text(harness + method + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(ROOT),
                    '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'lib/NativeApps/include'),
                    str(cpp), str(ROOT / 'src/runtime/streams/StreamRuntime.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
