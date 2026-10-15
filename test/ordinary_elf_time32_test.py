#!/usr/bin/env python3
"""Check the frozen clock consumer against Runtime's real time32 adapter."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--runtime", type=Path, required=True)
parser.add_argument("--cc", default="cc")
args = parser.parse_args()
adapter = args.runtime.resolve() / "lib/elf_loader/include/private/elf_time32.h"
if not adapter.is_file():
    parser.error("Selected Runtime does not contain its time32 ELF adapter")

native = r"""
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdint.h>
#include <errno.h>
_Static_assert(sizeof(time_t) == 8 && sizeof(struct timespec) == 16,
               "The integration test must use a native time64 host");
static int64_t seconds;
static int32_t nanoseconds;
static int fail;
static int native_clock_mock(clockid_t id, struct timespec *out) {
    (void)id;
    if (fail) return -1;
    out->tv_sec = seconds;
    out->tv_nsec = nanoseconds;
    return 0;
}
#define clock_gettime native_clock_mock
#include "elf_time32.h"
#undef clock_gettime
void runtime_fixture_set(int64_t sec, int32_t ns, int error) {
    seconds = sec; nanoseconds = ns; fail = error;
}
int runtime_clock32(int32_t id, void *out) {
    return risc_elf_clock_gettime32((clockid_t)id, out);
}
"""
consumer = r"""
#include <time.h>
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
void runtime_fixture_set(int64_t, int32_t, int);
int test_usleep(useconds_t micros) { (void)micros; return 0; }
int main(void) {
    struct { uint32_t before; struct timespec value; uint32_t after; } box;
    box.before = 0x12345678u; box.after = 0xabcdef01u;
    box.value.tv_sec = -7; box.value.tv_nsec = -9;
    runtime_fixture_set(2, 123456789, 0);
    assert(clock_gettime(CLOCK_MONOTONIC, &box.value) == 0);
    assert(box.value.tv_sec == 2 && box.value.tv_nsec == 123456789);
    assert(box.before == 0x12345678u && box.after == 0xabcdef01u);
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->start(NULL, 0));
    const risc_platform_clock_api_v1 *api = driver->capability;
    assert(api->monotonic_ms(api->context) == 2123u);
    runtime_fixture_set(INT32_MAX, 999999999, 0);
    assert(api->monotonic_ms(api->context) == (uint64_t)INT32_MAX * 1000u + 999u);
    runtime_fixture_set((int64_t)INT32_MAX + 1, 0, 0);
    assert(api->monotonic_ms(api->context) == UINT64_MAX);
    assert(clock_gettime(CLOCK_MONOTONIC, &box.value) == -1);
    assert(box.value.tv_sec == 2 && box.value.tv_nsec == 123456789);
    runtime_fixture_set(2, 1000000000, 0);
    assert(api->monotonic_ms(api->context) == UINT64_MAX);
    runtime_fixture_set(-1, 0, 0);
    assert(api->monotonic_ms(api->context) == UINT64_MAX);
    runtime_fixture_set(2, 0, 1);
    assert(api->monotonic_ms(api->context) == UINT64_MAX);
    assert(box.before == 0x12345678u && box.after == 0xabcdef01u);
    driver->stop();
    puts("Actual Runtime time64 adapter -> ordinary time32 consumer and clock lifecycle: PASS");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="ordinary-elf-time32-") as name:
    out = Path(name)
    (out / "native.c").write_text(native)
    (out / "consumer.c").write_text(consumer)
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-D_DEFAULT_SOURCE",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-pie"]
    subprocess.run([args.cc, *flags, "-I" + str(adapter.parent), "-c",
                    str(out / "native.c"), "-o", str(out / "native.o")], check=True)
    includes = ["-I" + str(ROOT / "sdk/compat/elf-time32"),
                "-I" + str(ROOT / "sdk/driver")]
    subprocess.run([args.cc, *flags, *includes, "-Dclock_gettime=runtime_clock32",
                    "-Dusleep=test_usleep", "-c", str(ROOT / "Drivers/platform_clock_v1/driver.c"),
                    "-o", str(out / "driver.o")], check=True)
    subprocess.run([args.cc, *flags, *includes, "-Dclock_gettime=runtime_clock32",
                    "-no-pie", str(out / "consumer.c"), str(out / "driver.o"),
                    str(out / "native.o"), "-o", str(out / "test")], check=True)
    subprocess.run([str(out / "test")], check=True)
