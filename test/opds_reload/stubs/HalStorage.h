#pragma once
#include <map>
#include <string>

struct String {
  std::string value;
  bool isEmpty() const { return value.empty(); }
  const char* c_str() const { return value.c_str(); }
};
struct StorageFixture {
  std::map<std::string, std::string> files;
  bool failRead = false;
  bool failWrite = false;
  int reads = 0;
  int writes = 0;
  void mkdir(const char*) {}
  bool exists(const char* path) const { return files.count(path) != 0; }
  String readFile(const char* path) {
    ++reads;
    return {failRead ? std::string{} : files.at(path)};
  }
  bool writeFile(const char* path, const std::string& value) {
    ++writes;
    if (failWrite) return false;
    files[path] = value;
    return true;
  }
};
extern StorageFixture Storage;
