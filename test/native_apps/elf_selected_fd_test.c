/* Host platform doubles for the production selected-descriptor loading path. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_dlfcn.h"
#include "private/esp_dlmod.h"
#include "private/elf_platform.h"

#define IMAGE_MAX (8 * 1024 * 1024)
static uint8_t disk[IMAGE_MAX], expected[IMAGE_MAX];
static bool live_fd[64];
static size_t positions[64];
static void *live_allocations[32];
static unsigned allocations, frees, allocation_calls, fail_allocation;
static unsigned opens, closes, reads, yields, admissions, relocations, deinits;
static unsigned lock_takes, fail_lock_take;
static TickType_t ticks;
static off_t image_size;
static int seek_failure, read_failure, short_at;
static bool close_failure, invalid_image, fail_admit, fail_init, fail_relocate;
static bool fail_mutex_create, timeout_read, timeout_seek, timeout_close;
static int admit_errno;
static const char *race_path;
static void *racing_handle;
static int admitted_fd;

static int descriptor(void)
{
    for (int fd = 7; fd < 64; ++fd) {
        if (!live_fd[fd]) { live_fd[fd] = true; positions[fd] = 0; return fd; }
    }
    assert(!"descriptor exhaustion"); return -1;
}

static void image(void)
{
    memset(disk, 0, sizeof(disk));
    elf32_hdr_t *h = (elf32_hdr_t *)disk;
    memcpy(h->ident, "\177ELF\1\1\1", 7);
    h->machine = 94; h->type = 3; h->version = 1;
    h->ehsize = sizeof(*h); h->shoff = sizeof(*h);
    h->shentsize = sizeof(elf32_shdr_t); h->shnum = 4; h->shstrndx = 1;
    elf32_shdr_t *s = (elf32_shdr_t *)(disk + h->shoff);
    const char names[] = "\0.shstrtab\0.text\0.dynsym\0";
    s[1].name = 1; s[1].type = SHT_STRTAB; s[1].offset = 256; s[1].size = sizeof(names);
    memcpy(disk + 256, names, sizeof(names));
    s[2].name = 11; s[2].type = SHT_PROGBITS; s[2].offset = 512;
    s[2].size = 4; s[2].addr = 0x1000; s[2].flags = SHF_ALLOC | SHF_EXECINSTR;
    s[3].name = 17; s[3].type = SHT_SYNSYM; s[3].offset = 516;
    s[3].size = sizeof(elf32_sym_t); s[3].link = 1;
    disk[512] = 0x42;
    memcpy(expected, disk, sizeof(disk));
}

static void reset(void)
{
    for (unsigned i = 0; i < 64; ++i) assert(!live_fd[i]);
    for (unsigned i = 0; i < 32; ++i) assert(!live_allocations[i]);
    assert(allocations == frees);
    assert(!dlmod_gethandle("same") && !dlmod_gethandle("race") && !dlmod_gethandle("new"));
    allocations = frees = allocation_calls = fail_allocation = 0;
    opens = closes = reads = yields = admissions = relocations = deinits = 0;
    lock_takes = fail_lock_take = 0; ticks = 0; image_size = 8193;
    seek_failure = read_failure = short_at = 0;
    close_failure = invalid_image = fail_admit = fail_init = fail_relocate = false;
    timeout_read = timeout_seek = timeout_close = false;
    fail_mutex_create = false; admit_errno = 0; race_path = NULL; racing_handle = NULL;
    admitted_fd = -1; errno = EDOM; image();
}

TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t delay) { assert(delay == 1); ++yields; ticks += delay; }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage)
{ return fail_mutex_create ? NULL : storage; }
int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{ assert(mutex && wait == portMAX_DELAY); return ++lock_takes != fail_lock_take; }
int xSemaphoreGive(SemaphoreHandle_t mutex) { assert(mutex); return pdTRUE; }

int fixture_open(const char *path, int flags, ...)
{ assert(path && flags == O_RDONLY); ++opens; return descriptor(); }
off_t fixture_lseek(int fd, off_t offset, int whence)
{
    assert(live_fd[fd] && offset == 0);
    if ((seek_failure == 1 && whence == SEEK_END) ||
            (seek_failure == 2 && whence == SEEK_SET)) { errno = ENXIO; return -1; }
    if (timeout_seek) ticks += 30000;
    if (whence == SEEK_END) return image_size;
    assert(whence == SEEK_SET); positions[fd] = 0; return 0;
}
ssize_t fixture_read(int fd, void *buffer, size_t count)
{
    assert(live_fd[fd] && count > 0 && count <= 4096);
    assert(positions[fd] + count <= (size_t)image_size);
    ++reads;
    if (read_failure) { errno = ENODEV; return -1; }
    if (timeout_read) ticks += 30000;
    memcpy(buffer, disk + positions[fd], count); positions[fd] += count;
    if (invalid_image) ((uint8_t *)buffer)[0] = 0;
    if (positions[fd] == (size_t)image_size) disk[image_size - 1] ^= 1;
    if (short_at == (int)reads) return count - 1;
    if (short_at == -1) return 0;
    return (ssize_t)count;
}
int fixture_close(int fd)
{
    assert(fd >= 0 && fd < 64 && live_fd[fd]);
    live_fd[fd] = false; ++closes;
    if (timeout_close) ticks += 30000;
    if (close_failure) { errno = EIO; return -1; }
    return 0;
}
void *esp_elf_malloc(uint32_t size, bool executable)
{
    assert(size && !executable);
    if (++allocation_calls == fail_allocation) return NULL;
    void *result = malloc(size); assert(result);
    for (unsigned i = 0; i < 32; ++i) if (!live_allocations[i]) {
        live_allocations[i] = result; ++allocations; return result;
    }
    assert(!"allocation accounting full"); return NULL;
}
void esp_elf_free(void *pointer)
{
    if (!pointer) return;
    for (unsigned i = 0; i < 32; ++i) if (live_allocations[i] == pointer) {
        live_allocations[i] = NULL; ++frees; free(pointer); errno = EDOM; return;
    }
    assert(!"untracked or double free");
}
int esp_elf_init(esp_elf_t *elf)
{ if (fail_init) return -EIO; memset(elf, 0, sizeof(*elf)); return 0; }
int esp_elf_relocate(esp_elf_t *elf, const uint8_t *bytes)
{
    ++relocations; assert(bytes != disk && bytes[512] == 0x42);
    elf->psegment = esp_elf_malloc(8, false);
    if (!elf->psegment) return -ENOMEM;
    memset(elf->psegment, 0x37, 8);
    if (race_path) {
        const char *path = race_path; race_path = NULL;
        racing_handle = dlopen(path, RTLD_NOW); assert(racing_handle);
    }
    return fail_relocate ? -ENOEXEC : 0;
}
void esp_elf_deinit(esp_elf_t *elf)
{ ++deinits; esp_elf_free(elf->psegment); memset(elf, 0, sizeof(*elf)); }
static bool admit(void *context, const uint8_t *bytes, size_t size)
{
    assert(context == &admitted_fd && admitted_fd >= 0 && !live_fd[admitted_fd]);
    assert(bytes && bytes != disk && size == (size_t)image_size);
    assert(!memcmp(bytes, expected, size)); ++admissions;
    errno = admit_errno; return !fail_admit;
}

static void snapshot(int expected_error)
{
    elf_file_t file = {.payload = (uint8_t *)1, .size = 17};
    admitted_fd = descriptor();
    int result = esp_elf_open_owned_fd(&file, admitted_fd, admit, &admitted_fd);
    assert(closes == 1 && opens == 0 && !live_fd[admitted_fd]);
    if (expected_error) {
        assert(result == -1 && errno == expected_error);
        assert(file.payload == (uint8_t *)1 && file.size == 17);
        assert(admissions == (fail_admit ? 1u : 0u));
    } else {
        assert(result == 0 && admissions == 1 && file.size == (size_t)image_size);
        assert(reads == (file.size + 4095) / 4096 && yields == reads);
        assert(file.payload != disk && !memcmp(file.payload, expected, file.size));
        esp_elf_close(&file); assert(!file.payload && !file.size);
    }
    assert(allocations == frees);
}

static void snapshot_cases(void)
{
    reset(); snapshot(0);
    reset(); ticks = UINT32_MAX - 2; snapshot(0);
    reset(); image_size = IMAGE_MAX; snapshot(0);
    reset(); image_size = 51; snapshot(EFBIG); assert(!reads && !allocations);
    reset(); image_size = IMAGE_MAX + 1; snapshot(EFBIG); assert(!reads && !allocations);
    reset(); image_size = 52; snapshot(ENOEXEC);
    reset(); seek_failure = 1; snapshot(ENXIO);
    reset(); seek_failure = 2; snapshot(ENXIO);
    reset(); short_at = 1; snapshot(EIO);
    reset(); short_at = 3; snapshot(EIO);
    reset(); short_at = -1; snapshot(EIO);
    reset(); read_failure = 1; close_failure = true; snapshot(ENODEV);
    reset(); timeout_read = true; snapshot(ETIMEDOUT); assert(reads == 1 && yields == 1);
    reset(); timeout_seek = true; snapshot(ETIMEDOUT); assert(!reads);
    reset(); timeout_close = true; snapshot(ETIMEDOUT);
    reset(); invalid_image = true; snapshot(ENOEXEC);
    reset(); close_failure = true; snapshot(EIO); assert(!admissions);
    reset(); fail_admit = true; snapshot(EACCES);
    reset(); fail_admit = true; admit_errno = EPERM; snapshot(EPERM);
    reset(); fail_allocation = 1; snapshot(ENOMEM);
    reset(); int fd = descriptor();
    assert(esp_elf_open_owned_fd(NULL, fd, admit, NULL) == -1 && errno == EINVAL);
    assert(closes == 1 && !reads);
    reset(); elf_file_t file = {0}; fd = descriptor(); close_failure = true;
    assert(esp_elf_open_owned_fd(&file, fd, NULL, NULL) == -1 && errno == EINVAL);
    assert(closes == 1 && !reads);
    reset(); assert(esp_elf_open_owned_fd(&file, -1, admit, NULL) == -1 && errno == EBADF);
    assert(!closes);
}

static void *selected(const char *name)
{
    admitted_fd = descriptor();
    return esp_dlopen_owned_fd(name, admitted_fd, admit, &admitted_fd);
}
static void require_module(void *handle, const char *name)
{
    assert(handle && dlmod_validate_handle(handle) && dlmod_gethandle(name) == handle);
    struct dlmod_slist_t *module = handle;
    assert(module->elf && module->elf->psegment[0] == 0x37);
}
static void loader_cases(void)
{
    reset(); void *handle = selected("/selected/new.elf");
    require_module(handle, "new"); assert(!opens && admissions == 1 && closes == 1);
    assert(dlerror() == NULL && dlclose(handle) == 0);

    reset(); void *old = dlopen("/legacy/same.elf", RTLD_NOW); require_module(old, "same");
    unsigned before_reads = reads, before_closes = closes, before_allocs = allocations;
    assert(!selected("/selected/same.elf") && errno == EEXIST);
    assert(opens == 1 && reads == before_reads && closes == before_closes + 1);
    assert(!admissions && allocations == before_allocs && dlerror());
    require_module(old, "same"); assert(dlclose(old) == 0);

    reset(); race_path = "/legacy/race.elf";
    assert(!selected("/selected/race.elf") && errno == EEXIST);
    require_module(racing_handle, "race");
    assert(opens == 1 && closes == 2 && admissions == 1 && relocations == 2 && deinits == 1);
    assert(dlclose(racing_handle) == 0);

    /* Ordinary dlopen still reuses existing basename modules, in both checks. */
    reset(); old = dlopen("/legacy/same.elf", RTLD_NOW); assert(old);
    assert(dlopen("/different/same.elf", RTLD_NOW) == old && opens == 1);
    assert(dlclose(old) == 0);
    reset(); race_path = "/other/race.elf";
    handle = dlopen("/legacy/race.elf", RTLD_NOW);
    assert(handle && handle == racing_handle && relocations == 2 && deinits == 1);
    assert(dlclose(handle) == 0);

    /* A selected route cannot silently fall back to the weak managed hook. */
    reset(); assert(!dlopen("/sd/Apps/new/new.elf", RTLD_NOW));
    assert(opens == 1 && closes == 1 && !relocations);
    reset(); handle = dlopen("/sd/Apps/new.elf", RTLD_NOW); assert(handle); assert(dlclose(handle) == 0);
    reset(); handle = selected("/sd/Apps/new/new.elf"); assert(handle && opens == 0); assert(dlclose(handle) == 0);

    const char *bad_names[] = {NULL, "", "/", "/selected/.elf",
        "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl.elf"};
    for (unsigned i = 0; i < sizeof(bad_names) / sizeof(*bad_names); ++i) {
        reset(); assert(!selected(bad_names[i]) && errno == EINVAL);
        assert(closes == 1 && !opens && !reads && !admissions);
    }
    reset(); int fd = descriptor(); close_failure = true;
    assert(!esp_dlopen_owned_fd("new", fd, NULL, NULL) && errno == EINVAL);
    assert(closes == 1 && !reads);
    reset(); assert(!esp_dlopen_owned_fd("new", -1, admit, NULL) && errno == EBADF);
    assert(!closes);
    reset(); fd = descriptor(); assert(!dlmod_insert_owned_fd(NULL, fd, admit, NULL));
    assert(errno == EINVAL && closes == 1);

    for (unsigned fail = 1; fail <= 4; ++fail) {
        reset(); fail_allocation = fail;
        assert(!selected("new.elf") && errno == ENOMEM);
        assert(closes == 1 && !opens && allocations == frees);
    }
    reset(); fail_init = true; assert(!selected("new") && errno == EIO);
    assert(closes == 1 && allocations == frees && !deinits);
    reset(); fail_relocate = true; assert(!selected("new") && errno == ENOEXEC);
    assert(closes == 1 && allocations == frees && deinits == 1);
    reset(); close_failure = true; assert(!selected("new") && errno == EIO);
    assert(closes == 1 && !admissions && !relocations);
    reset(); fail_admit = true; assert(!selected("new") && errno == EACCES);
    assert(closes == 1 && admissions == 1 && !relocations);
    reset(); fail_lock_take = 1; assert(!selected("new") && errno == EBUSY);
    assert(closes == 1 && !reads);
    reset(); fail_lock_take = 2; assert(!selected("new") && errno == EBUSY);
    assert(closes == 1 && allocations == frees && deinits == 1);
    reset();
}
int main(void)
{
    /* Before lazy dlmod mutex initialization, test creation failure. */
    image(); image_size = 8193; fail_mutex_create = true;
    admitted_fd = descriptor();
    assert(!esp_dlopen_owned_fd("new", admitted_fd, admit, &admitted_fd) && errno == EBUSY);
    assert(closes == 1 && !reads); fail_mutex_create = false;
    snapshot_cases(); loader_cases();
    puts("Selected ELF owned-fd reader and dlmod: bounded copy, real structural validation, checked close, mandatory admission, both collisions, cleanup and legacy regressions PASS");
}
