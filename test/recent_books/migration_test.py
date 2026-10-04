#!/usr/bin/env python3
"""Build the complete production RecentBooksStore and binary reader with host I/O fixtures."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'src/RecentBooksStore.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='recent-migration-') as tmp:
    out = Path(tmp)
    (out / 'HalStorage.h').write_text(r'''#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
extern int liveFiles;
extern std::vector<std::string> events;
struct String {
  std::string value;
  bool isEmpty() const { return value.empty(); }
  const char* c_str() const { return value.c_str(); }
};
struct FsFile {
  std::string bytes; size_t pos=0; bool opened=false;
  ~FsFile() { close(); }
  void close() { if (opened) { opened=false; --liveFiles; events.push_back("close"); } }
  size_t read(void* target, size_t n) {
    if (!opened) return 0;
    n=std::min(n, bytes.size()-pos); memcpy(target, bytes.data()+pos, n); pos+=n; return n;
  }
  size_t write(const uint8_t*, size_t n) { return n; }
};
struct StorageFixture {
  std::map<std::string,std::string> files;
  bool failOpen=false, failRename=false;
  int opens=0, renames=0;
  void mkdir(const char*) {}
  bool exists(const char* path) const { return files.count(path)!=0; }
  String readFile(const char* path) const { return {files.at(path)}; }
  bool openFileForRead(const char*, const char* path, FsFile& file) {
    ++opens; events.push_back("open");
    if (failOpen || !exists(path)) return false;
    file.bytes=files.at(path); file.opened=true; ++liveFiles; return true;
  }
  bool rename(const char* from, const char* to) {
    ++renames; events.push_back("rename");
    if (failRename || !exists(from) || exists(to)) return false;
    files[to]=files.at(from); files.erase(from); return true;
  }
};
extern StorageFixture Storage;
''')
    (out / 'Logging.h').write_text('''#pragma once
#include <string>
#include <vector>
extern std::vector<std::string> logs;
#define LOG_DBG(tag, message, ...) logs.emplace_back(message)
#define LOG_ERR(tag, message, ...) logs.emplace_back(message)
''')
    (out / 'JsonSettingsIO.h').write_text('''#pragma once
class RecentBooksStore;
namespace JsonSettingsIO {
bool loadRecentBooks(RecentBooksStore&, const char*);
bool saveRecentBooks(const RecentBooksStore&, const char*);
}
''')
    (out / 'FsHelpers.h').write_text('''#pragma once
#include <string>
namespace FsHelpers {
inline bool hasEpubExtension(const std::string&) { return false; }
inline bool hasXtcExtension(const std::string&) { return false; }
inline bool isPlainTextReadable(const std::string&) { return true; }
}
''')
    for name in ('Epub', 'Xtc'):
        (out / (name + '.h')).write_text('''#pragma once
#include <string>
class NAME { public:
NAME(const std::string&, const char*) {}
bool load(bool=false, bool=false) { return false; }
std::string getTitle() const { return {}; }
std::string getAuthor() const { return {}; }
std::string getThumbBmpPath() const { return {}; }
};
'''.replace('NAME', name))
    # Include the unmodified source file, its real class header and real binary
    # serializer; only storage, JSON publication and book metadata are fixtures.
    (out / 'test.cpp').write_text('#include "' + str(args.source.resolve()) + '"\n' + r'''
#include <cstdlib>
#include <sstream>
#include <cstdio>
int liveFiles=0;
std::vector<std::string> events, logs;
StorageFixture Storage;
int saves=0, jsonLoads=0;
bool failSave=false;
std::vector<RecentBook> published;
constexpr const char* BIN="/.crosspoint/recent.bin";
constexpr const char* JSON="/.crosspoint/recent.json";
constexpr const char* BAK="/.crosspoint/recent.bin.bak";
void require(bool ok, const char* what) {
  if (!ok) { fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
namespace JsonSettingsIO {
bool saveRecentBooks(const RecentBooksStore& store, const char* path) {
  ++saves; events.push_back("save");
  require(liveFiles==0,"legacy descriptor closed before JSON publication");
  if (failSave) return false;
  published=store.getBooks(); Storage.files[path]="valid"; return true;
}
bool loadRecentBooks(RecentBooksStore& store, const char* json) {
  ++jsonLoads;
  if (std::string(json)!="valid") return false;
  store.recentBooks=published; return true;
}
}
void reset() {
  require(liveFiles==0,"no file leaked across attempts");
  Storage={}; events.clear(); logs.clear(); published.clear();
  saves=jsonLoads=0; failSave=false;
}
std::string legacy(bool omit=false, uint8_t version=3) {
  std::ostringstream out;
  serialization::writePod(out,version);
  const uint8_t count=2; serialization::writePod(out,count);
  for (int i=0;i<2;++i) {
    serialization::writeString(out,i?"/b.epub":"/a.epub");
    serialization::writeString(out,omit && i==0?"":(i?"Book B":"Book A"));
    serialization::writeString(out,i?"Author B":"Author A");
    serialization::writeString(out,i?"/cache/b.bmp":"/cache/a.bmp");
  }
  return out.str();
}
bool successLogged() {
  return std::find(logs.begin(),logs.end(),"Migrated recent.bin to recent.json")!=logs.end();
}
int main() {
  // The original production source fails this case by renaming the only store.
  reset(); const std::string bytes=legacy(); Storage.files[BIN]=bytes; failSave=true;
  RecentBooksStore failed;
  require(!failed.loadFromFile(),"failed JSON publication must fail migration");
  require(Storage.files.at(BIN)==bytes && !Storage.exists(BAK),"failed save preserves exact legacy bytes");
  require(Storage.renames==0 && !successLogged(),"no retirement/success log after failed save");
  require(failed.getCount()==2 && liveFiles==0,"loaded records retained in RAM and file closed");
  require(!failed.loadFromFile() && Storage.renames==0,"repeated failed save remains retryable");
  failSave=false; RecentBooksStore reboot;
  require(reboot.loadFromFile(),"fresh boot retries preserved legacy source");
  require(!Storage.exists(BIN) && Storage.files.at(BAK)==bytes && Storage.exists(JSON),"successful publication precedes retirement");
  require(published.size()==2 && published[1].path=="/b.epub" && published[1].title=="Book B", "real v3 reader preserves both records");
  require(events.size()>=4 && events[events.size()-2]=="save" && events.back()=="rename", "publish before rename ordering");
  const int oldSaves=saves, oldOpens=Storage.opens, oldRenames=Storage.renames;
  RecentBooksStore nextBoot;
  require(nextBoot.loadFromFile() && nextBoot.getCount()==2,"next boot reads published JSON");
  require(saves==oldSaves && Storage.opens==oldOpens && Storage.renames==oldRenames,"JSON path has no migration side effects");

  reset(); Storage.files[BIN]=bytes; Storage.failRename=true;
  RecentBooksStore renameFailure;
  require(!renameFailure.loadFromFile(),"failed retirement is reported");
  require(Storage.files.at(BIN)==bytes && Storage.exists(JSON) && !successLogged(),"rename failure retains source and replacement");
  const int savesAtFailure=saves;
  Storage.failRename=false;
  require(renameFailure.loadFromFile() && saves==savesAtFailure,"retry can use valid published JSON without rewriting");

  reset(); Storage.files[BIN]=bytes; Storage.files[BAK]="older backup";
  RecentBooksStore collision;
  require(!collision.loadFromFile() && Storage.files.at(BAK)=="older backup" && Storage.files.at(BIN)==bytes,"failed backup replacement loses neither source nor existing backup");

  reset(); Storage.files[BIN]=bytes; Storage.failOpen=true;
  RecentBooksStore openFailure;
  require(!openFailure.loadFromFile() && saves==0 && Storage.renames==0 && liveFiles==0,"open failure does not publish or retire");
  Storage.failOpen=false;
  require(openFailure.loadFromFile(),"open failure recovery");

  reset(); Storage.files[BIN]=std::string(1,'\x7f');
  RecentBooksStore invalid;
  require(!invalid.loadFromFile() && saves==0 && Storage.renames==0 && liveFiles==0,"unknown binary version preserves source and closes descriptor");

  reset(); Storage.files[BIN]=legacy(true); failSave=true;
  const auto omittedBytes=Storage.files.at(BIN);
  RecentBooksStore omitted;
  require(!omitted.loadFromFile() && Storage.files.at(BIN)==omittedBytes && Storage.renames==0,"v3 omitted-title resave failure still preserves migration source");
  failSave=false;
  require(omitted.loadFromFile() && published.size()==1 && published[0].title=="Book B", "omitted-title retry publishes valid retained row");

  reset(); Storage.files[JSON]="invalid"; Storage.files[BIN]=bytes;
  RecentBooksStore malformedJson;
  require(!malformedJson.loadFromFile() && Storage.opens==0 && saves==0 && Storage.renames==0,"nonempty JSON keeps existing validation/precedence behavior");
  reset(); Storage.files[JSON]=""; Storage.files[BIN]=bytes;
  RecentBooksStore emptyJson;
  require(emptyJson.loadFromFile(),"empty JSON retains existing binary fallback");
  reset(); RecentBooksStore absent;
  require(!absent.loadFromFile() && saves==0 && Storage.renames==0,"no-store behavior unchanged");
  require(liveFiles==0,"all descriptors closed");
  std::puts("PASS: complete RecentBooksStore migration save/rename failures, reboot/retry, JSON precedence and cleanup");
}
''')
    flags = shlex.split(os.environ.get('CXXFLAGS', ''))
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', *flags, '-I'+str(out), '-I'+str(ROOT/'src'),
               '-I'+str(ROOT/'lib/Serialization'), str(out/'test.cpp'), '-o', str(out/'test')]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(out/'test')], check=True, timeout=30)
