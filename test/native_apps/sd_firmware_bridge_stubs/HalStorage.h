#pragma once

#include <cstddef>
#include <string>

extern std::string sd_firmware_opened_path;
extern std::string sd_firmware_opened_system;
extern size_t sd_firmware_file_size;
extern bool sd_firmware_file_open_succeeds;
extern int sd_firmware_file_close_count;

class HalFile {
public:
    bool is_open = false;
    explicit operator bool() const { return is_open; }
    size_t fileSize() const { return sd_firmware_file_size; }
    void close() { is_open = false; ++sd_firmware_file_close_count; }
};

class sd_firmware_fake_storage_t {
public:
    bool openFileForRead(const char *system, const char *path, HalFile &file);
};

extern sd_firmware_fake_storage_t Storage;
