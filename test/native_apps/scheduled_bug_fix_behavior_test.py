#!/usr/bin/env python3
"""Execute production fix functions with fault-injected host dependencies."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]

def function(path, signature):
    source = (repo / path).read_text()
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include "text_editor_core.h"
#define T5_APP_BUTTON_CONFIRM 2u
struct tc_day_t { int32_t ymd; int16_t punches[4]; };
static te_document document;
static char path[160], filename[64], scratch[TE_CAPACITY + 1];
static size_t first_row;
static void report(const char*) {}
static std::string saved;
static bool read_ok = true, short_read = false;
static bool read_file(const char*, void* p, size_t capacity, size_t* count) {
    if (!read_ok) return false;
    *count = saved.size();
    if (p) {
        if (short_read && *count) --*count;
        if (*count > capacity) return false;
        memcpy(p, saved.data(), *count);
    }
    return true;
}
struct StorageApi { bool (*read_file)(const char*, void*, size_t, size_t*); };
static StorageApi storage_api{read_file};
static StorageApi* storage = &storage_api;
static uint32_t ticks, read_cost, yields;
static uint32_t millis() { return ticks; }
static void vTaskDelay(unsigned n) { ticks += n; ++yields; }
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
static std::vector<uint8_t> file_bytes;
static bool fail_read, open_ok = true;
static uint64_t fake_size;
struct FsFile {
    size_t position = 0;
    uint64_t fileSize() const { return fake_size ? fake_size : file_bytes.size(); }
    int read(uint8_t* p, size_t n) {
        ticks += read_cost;
        if (fail_read || position == file_bytes.size()) return 0;
        n = std::min(n, file_bytes.size() - position);
        memcpy(p, file_bytes.data() + position, n);
        position += n;
        return (int)n;
    }
    void close() {}
};
struct StorageMock { bool openFileForRead(const char*, const char*, FsFile&) { return open_ok; } } Storage;
// CRC is supplied by ESP ROM in production; use a deterministic rolling hash
// here to test chunk/EOF/control flow independently of that ROM implementation.
static uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* p, uint32_t n) {
    while (n--) crc = crc * 33u + *p++;
    return crc;
}
'''
code += function('Apps/timecard.c', 'static int16_t worked(')
code += function('Apps/font_manager.c', 'static bool confirm_rising_edge(')
code += function('Apps/text_editor.c', 'static bool discard_changes(')
code += function('src/native/NativeFontBridge.cpp', 'bool computeCrc32(')
code += r'''
int main() {
    const int16_t cases[][5] = {
        {540,720,780,1020,420}, {540,420,480,1020,480},
        {540,1080,1140,1020,480}, {540,510,570,1020,450},
        {540,990,1050,1020,450}, {540,480,1080,1020,0},
        {540,480,540,1020,480}, {540,1020,1080,1020,480},
        {540,-1,-1,1020,480}, {540,780,720,1020,480}
    };
    for (const auto& c : cases) {
        tc_day_t day{20260927, {c[0],c[1],c[2],c[3]}};
        assert(worked(&day) == c[4]);
    }
    assert(worked(nullptr) == -1);
    bool held = false;
    assert(confirm_rising_edge(T5_APP_BUTTON_CONFIRM, &held));
    for (int i=0; i<10; ++i) assert(!confirm_rising_edge(T5_APP_BUTTON_CONFIRM, &held));
    assert(!confirm_rising_edge(0, &held));
    assert(confirm_rising_edge(T5_APP_BUTTON_CONFIRM, &held));
    strcpy(path, "/sd/Documents/test.txt"); strcpy(filename, "test.txt");
    saved = "original";
    assert(te_import(&document, "edits", 5)); document.dirty = true;
    assert(discard_changes());
    assert(!document.dirty && strcmp(document.text, "original") == 0);
    assert(te_import(&document, "edits", 5)); document.dirty = true;
    read_ok = false;
    assert(!discard_changes()); // Missing file or disconnected SD preserves edits.
    assert(document.dirty && strcmp(document.text, "edits") == 0 && path[0]);
    read_ok = true; short_read = true;
    assert(!discard_changes()); assert(document.dirty);
    short_read = false; saved = std::string("bad\0text", 8);
    assert(!discard_changes()); assert(document.dirty && strcmp(document.text, "edits") == 0);
    path[0] = 0;
    assert(discard_changes()); assert(!document.dirty && document.length == 0 && !filename[0]);
    file_bytes.resize(9000);
    for (size_t i=0; i<file_bytes.size(); ++i) file_bytes[i] = (uint8_t)i;
    uint32_t checksum = 0;
    assert(computeCrc32("font", checksum));
    assert(checksum == esp_rom_crc32_le(0, file_bytes.data(), file_bytes.size()));
    assert(yields >= 2);
    checksum = 123; fail_read = true;
    assert(!computeCrc32("font", checksum) && checksum == 123);
    fail_read = false; fake_size = file_bytes.size() + 1;
    assert(!computeCrc32("font", checksum)); // EOF before the snapshotted length.
    fake_size = 65u * 1024u * 1024u;
    assert(!computeCrc32("font", checksum));
    fake_size = 0; ticks = 0; read_cost = 31000;
    assert(!computeCrc32("font", checksum));
    read_cost = 0; ticks = 0; file_bytes.clear();
    assert(computeCrc32("font", checksum) && checksum == 0);
    puts("Production scheduled-fix behavior and failure paths PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    source = Path(temp) / 'fixes.cpp'
    binary = Path(temp) / 'fixes'
    source.write_text(code)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(repo / 'Apps'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
