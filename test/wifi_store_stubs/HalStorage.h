#pragma once
#include <map>
#include <string>
struct String {
  std::string value;
  String() = default;
  String(std::string v) : value(std::move(v)) {}
  bool isEmpty() const { return value.empty(); }
  const char* c_str() const { return value.c_str(); }
};
struct FsFile {};
class HalStorage {
 public:
  static HalStorage& getInstance() { static HalStorage value; return value; }
  std::map<std::string, std::string> files;
  unsigned failWrites = 0;
  unsigned failRenameToJson = 0;
  void mkdir(const char*) {}
  bool exists(const char* path) const { return files.count(path) != 0; }
  String readFile(const char* path) { auto i=files.find(path); return i==files.end()?String{}:String{i->second}; }
  bool writeFile(const char* path, const String& value) {
    if (failWrites) { --failWrites; return false; }
    files[path]=value.value; return true;
  }
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool rename(const char* oldPath, const char* newPath) {
    if (std::string(newPath)=="/.crosspoint/wifi.json" && failRenameToJson) { --failRenameToJson; return false; }
    auto i=files.find(oldPath); if(i==files.end() || files.count(newPath)) return false;
    files[newPath]=i->second; files.erase(i); return true;
  }
  bool openFileForRead(const char*, const char*, FsFile&) { return false; }
};
#define Storage HalStorage::getInstance()
