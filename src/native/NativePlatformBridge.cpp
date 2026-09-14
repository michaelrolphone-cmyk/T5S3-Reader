#include <Arduino.h>
#include <HalStorage.h>
#include <T5StorageApi.h>
#include <T5SystemApi.h>

#include <cstring>
#include <ctime>
#include <string>

namespace {

bool mapStoragePath(const char* path, std::string& mapped) {
  if (!path) return false;
  if (std::strcmp(path, "/sd") == 0) {
    mapped = "/";
    return true;
  }
  if (std::strncmp(path, "/sd/", 4) != 0) return false;

  const char* relative = path + 3;
  const char* segment = relative + 1;
  if (*segment == '\0') return false;
  while (*segment) {
    const char* end = segment;
    while (*end && *end != '/') {
      if (*end == '\\') return false;
      ++end;
    }
    const size_t length = static_cast<size_t>(end - segment);
    if (length == 0 || (length == 1 && segment[0] == '.') ||
        (length == 2 && segment[0] == '.' && segment[1] == '.')) {
      return false;
    }
    if (!*end) break;
    segment = end + 1;
    if (*segment == '\0') return false;
  }

  mapped = relative;
  return true;
}

bool ensureParentDirectory(const std::string& path) {
  const size_t separator = path.find_last_of('/');
  if (separator == std::string::npos || separator == 0) return true;
  const std::string parent = path.substr(0, separator);
  return Storage.ensureDirectoryExists(parent.c_str());
}

bool storageExists(const char* path) {
  if (!Storage.ready()) return false;
  std::string mapped;
  return mapStoragePath(path, mapped) && Storage.exists(mapped.c_str());
}

bool readFile(const char* path, void* buffer, size_t capacity, size_t* outSize) {
  if (outSize) *outSize = 0;
  if (!outSize || !Storage.ready()) return false;

  std::string mapped;
  if (!mapStoragePath(path, mapped) || !Storage.exists(mapped.c_str())) return false;

  const String contents = Storage.readFile(mapped.c_str());
  const size_t size = contents.length();
  *outSize = size;

  if (!buffer || capacity == 0) return true;
  if (size > capacity) return false;
  if (size != 0) std::memcpy(buffer, contents.c_str(), size);
  return true;
}

bool writeFileAtomic(const char* path, const void* data, size_t size) {
  if ((!data && size != 0) || !Storage.ready()) return false;

  std::string destination;
  if (!mapStoragePath(path, destination) || destination == "/") return false;
  if (!ensureParentDirectory(destination)) return false;

  const std::string temporary = destination + ".part";
  const std::string backup = destination + ".bak";

  if (Storage.exists(backup.c_str())) {
    if (!Storage.exists(destination.c_str())) {
      if (!Storage.rename(backup.c_str(), destination.c_str())) return false;
    } else {
      Storage.remove(backup.c_str());
    }
  }
  if (Storage.exists(temporary.c_str())) Storage.remove(temporary.c_str());

  String contents;
  if (!contents.reserve(size + 1)) return false;
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) contents += static_cast<char>(bytes[i]);

  if (!Storage.writeFile(temporary.c_str(), contents)) {
    Storage.remove(temporary.c_str());
    return false;
  }

  const bool hadExisting = Storage.exists(destination.c_str());
  if (hadExisting && !Storage.rename(destination.c_str(), backup.c_str())) {
    Storage.remove(temporary.c_str());
    return false;
  }

  if (!Storage.rename(temporary.c_str(), destination.c_str())) {
    Storage.remove(temporary.c_str());
    if (hadExisting) Storage.rename(backup.c_str(), destination.c_str());
    return false;
  }

  if (hadExisting && Storage.exists(backup.c_str())) Storage.remove(backup.c_str());
  return true;
}

bool removeFile(const char* path) {
  if (!Storage.ready()) return false;
  std::string mapped;
  if (!mapStoragePath(path, mapped) || mapped == "/") return false;
  if (!Storage.exists(mapped.c_str())) return true;
  return Storage.remove(mapped.c_str());
}

bool localDateTime(t5_local_datetime_t* out) {
  if (!out) return false;
  const time_t now = time(nullptr);
  if (now <= 0) return false;

  struct tm local = {};
  if (!localtime_r(&now, &local)) return false;
  out->year = static_cast<int16_t>(local.tm_year + 1900);
  out->month = static_cast<uint8_t>(local.tm_mon + 1);
  out->day = static_cast<uint8_t>(local.tm_mday);
  out->hour = static_cast<uint8_t>(local.tm_hour);
  out->minute = static_cast<uint8_t>(local.tm_min);
  out->second = static_cast<uint8_t>(local.tm_sec);
  out->weekday = static_cast<uint8_t>(local.tm_wday);
  out->yearday = static_cast<uint16_t>(local.tm_yday);
  return true;
}

const t5_storage_api_v1 kStorageApi = {
    T5_STORAGE_API_VERSION,
    sizeof(t5_storage_api_v1),
    storageExists,
    readFile,
    writeFileAtomic,
    removeFile,
};

const t5_system_api_v1 kSystemApi = {
    T5_SYSTEM_API_VERSION,
    sizeof(t5_system_api_v1),
    localDateTime,
};

}  // namespace

extern "C" const t5_storage_api_v1* t5_storage_get_api(uint32_t apiVersion) {
  return apiVersion == T5_STORAGE_API_VERSION ? &kStorageApi : nullptr;
}

extern "C" const t5_system_api_v1* t5_system_get_api(uint32_t apiVersion) {
  return apiVersion == T5_SYSTEM_API_VERSION ? &kSystemApi : nullptr;
}
