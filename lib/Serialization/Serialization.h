#pragma once
#include <HalStorage.h>

#include <iostream>

class HalReadBudget;

namespace serialization {
template <typename T>
static void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
static void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

static void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

static void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

static void readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  s.resize(len);
  is.read(&s[0], len);
}

static void readString(FsFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);
  s.resize(len);
  file.read(&s[0], len);
}
// Explicit opt-in only. Keep the ordinary serializers and their I/O contract
// unchanged; the operation owns the budget and no bytes are prefetched.
template <typename File, typename T>
static void readPod(File& file, T& value, HalReadBudget& budget) {
  file.readCooperatively(reinterpret_cast<uint8_t*>(&value), sizeof(T), budget);
}

template <typename File>
static void readString(File& file, std::string& s, HalReadBudget& budget) {
  uint32_t len;
  readPod(file, len, budget);
  s.resize(len);
  file.readCooperatively(&s[0], len, budget);
}
}  // namespace serialization
