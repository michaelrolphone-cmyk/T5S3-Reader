#!/usr/bin/env python3
"""Exercise the production copy function with the ESP32's 32-bit size limit."""
from pathlib import Path
import os
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/file_browser.c").read_text()
start = source.index("static bool copy_usb_to_sd(")
end = source.index("\nstatic bool copy_selected(", start)
function = source[start:end]
harness = r'''
#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "RiscStorageVolumeV1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Host size_t can be 64-bit; model the target's bound without changing code. */
#undef SIZE_MAX
#define SIZE_MAX UINT32_MAX
static uint64_t reported_size;
static bool input_valid = true, output_valid = true;
static unsigned closes, opens, commits, reads;
static char status_text[160];
static unsigned char copy_buffer[4096];
static bool storage_write_stream_available(void) { return true; }
static bool refresh_usb(void) { return true; }
static void copy_text(char *out, size_t cap, const char *text) { snprintf(out, cap, "%s", text); }
static risc_storage_file_t input_open(void *ctx, const char *path, uint64_t *size) {
    (void)ctx; (void)path; *size = reported_size; return input_valid ? 7 : 0;
}
static bool input_close(void *ctx, risc_storage_file_t handle, bool commit) {
    (void)ctx; assert(handle == 7); assert(commit); ++closes; return true;
}
static size_t input_read(void *ctx, risc_storage_file_t handle, void *out, size_t count) {
    (void)ctx; assert(handle == 7); memset(out, 0, count); ++reads; return count;
}
static t5_storage_stream_t output_open(const char *path) {
    (void)path; ++opens; return output_valid ? 3 : 0;
}
static size_t output_write(t5_storage_stream_t handle, const void *data, size_t count) {
    assert(handle == 3); (void)data; return count;
}
static bool output_commit(t5_storage_stream_t handle) { assert(handle == 3); ++commits; return true; }
static void output_abort(t5_storage_stream_t handle) { (void)handle; assert(0); }
static const risc_storage_volume_api_v1 volume = {
    .file_open_read = input_open, .file_close = input_close, .file_read = input_read
};
static const t5_storage_api_v1 store = {
    .write_stream_open = output_open, .write_stream_write = output_write,
    .write_stream_commit = output_commit, .write_stream_abort = output_abort
};
static const t5_app_api_v1 application = {0};
static const risc_storage_volume_api_v1 *usb_volume = &volume;
static const t5_storage_api_v1 *storage = &store;
static const t5_app_api_v1 *app = &application;
'''
harness += function + r'''
static void reset(void) { closes = opens = commits = reads = 0; status_text[0] = 0; }
int main(void) {
    /* Failed open must not close a nonexistent input or create the destination. */
    input_valid = false; reported_size = 0; reset();
    assert(!copy_usb_to_sd("source", "destination")); assert(closes == 0 && opens == 0);
    input_valid = true;
    /* Repeated oversized attempts release exactly one handle each, before any write. */
    reported_size = (uint64_t)UINT32_MAX + 1; reset();
    for (unsigned i = 0; i < 32; ++i) {
        assert(!copy_usb_to_sd("source", "destination"));
        assert(closes == i + 1 && opens == 0 && reads == 0 && commits == 0);
        assert(strstr(status_text, "too large"));
    }
    /* Boundary is accepted: failed destination creation still closes the input. */
    reported_size = UINT32_MAX; output_valid = false; reset();
    assert(!copy_usb_to_sd("source", "destination")); assert(closes == 1 && opens == 1);
    /* Ordinary and empty files still close and commit once. */
    output_valid = true; reported_size = 5; reset();
    assert(copy_usb_to_sd("source", "destination"));
    assert(closes == 1 && opens == 1 && reads == 1 && commits == 1);
    reported_size = 0; reset();
    assert(copy_usb_to_sd("source", "destination"));
    assert(closes == 1 && opens == 1 && reads == 0 && commits == 1);
    puts("File Browser USB handle runtime regression PASS");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(harness)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(repo / "lib/NativeApps/include"),
                    "-I" + str(repo / "sdk/driver"), str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)
