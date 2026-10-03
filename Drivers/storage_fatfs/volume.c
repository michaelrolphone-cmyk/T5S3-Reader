/* Included by driver.c; FatFs is linked into this ELF, not into the host HAL. */
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

#define FILE_SLOTS 12u
#define DIR_SLOTS 8u
#define OP_BUDGET_MS 15000u
#define OP_SECTOR_LIMIT 2048u
static FATFS filesystem;
static uint64_t operation_start;
static uint32_t operation_steps, operation_sectors;
static bool operation_busy;
static bool power_down_prepared;
static uint32_t next_generation = 1;
typedef struct {
    FIL object;
    uint32_t handle, flags, error;
    bool abortable;
    char path[RISC_STORAGE_VOLUME_PATH_MAX];
} file_slot;
typedef struct { DIR object; uint32_t handle, error; char path[RISC_STORAGE_VOLUME_PATH_MAX]; } dir_slot;
static file_slot files[FILE_SLOTS];
static dir_slot dirs[DIR_SLOTS];
static bool has_handles(void) {
    for (unsigned i = 0; i < FILE_SLOTS; ++i) if (files[i].handle) return true;
    for (unsigned i = 0; i < DIR_SLOTS; ++i) if (dirs[i].handle) return true;
    return false;
}
static bool enter_lifecycle(void) {
    if (__atomic_test_and_set(&operation_busy, __ATOMIC_ACQUIRE)) return false;
    operation_start = clock_api ? clock_api->monotonic_ms(clock_api->context) : 0;
    operation_steps = operation_sectors = 0;
    return true;
}
static void leave(void) { __atomic_clear(&operation_busy, __ATOMIC_RELEASE); }
static bool enter(void) {
    if (!enter_lifecycle()) return false;
    if (power_down_prepared) { leave(); return false; }
    return true;
}
int risc_fatfs_checkpoint(void) {
    if (!clock_api || io_failed) return 0;
    if ((++operation_steps & 255u) == 0) cooperate(4096);
    else cooperate(0);
    if (operation_steps > 1048576u ||
        clock_api->monotonic_ms(clock_api->context) - operation_start >= OP_BUDGET_MS) {
        fail("filesystem operation budget exceeded"); io_failed = true; mounted = false; return 0;
    }
    return 1;
}
static bool disk_budget(void) {
    if (++operation_sectors > OP_SECTOR_LIMIT || !risc_fatfs_checkpoint()) {
        fail("filesystem I/O budget exceeded"); io_failed = true; mounted = false; return false;
    }
    cooperate(0);
    return true;
}
DSTATUS disk_initialize(BYTE drive) { return drive || !card_ready || io_failed ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t lba, UINT count) {
    if (disk_status(drive) || !buffer || !count || count > 128u || lba > UINT32_MAX - count) return RES_PARERR;
    for (UINT i = 0; i < count; ++i) {
        if (!disk_budget() || !read_sector((uint32_t)lba + i, buffer + i * 512u)) {
            fail("SD sector read/CRC failure"); io_failed = true; mounted = false; return RES_ERROR;
        }
    }
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t lba, UINT count) {
    if (disk_status(drive) || !buffer || !count || count > 128u || lba > UINT32_MAX - count) return RES_PARERR;
    for (UINT i = 0; i < count; ++i) {
        if (!disk_budget() || !write_sector((uint32_t)lba + i, buffer + i * 512u)) {
            fail("SD write uncertain; remount required"); io_failed = true; mounted = false; return RES_ERROR;
        }
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command_code, void *buffer) {
    (void)buffer;
    if (disk_status(drive)) return RES_NOTRDY;
    return command_code == CTRL_SYNC && sync_card() ? RES_OK : RES_ERROR;
}
static bool result_ok(FRESULT result) {
    if (result == FR_OK) return true;
    if (result == FR_DISK_ERR || result == FR_INT_ERR || result == FR_TIMEOUT) {
        io_failed = true; mounted = false;
        if (!error[0]) fail("filesystem media/integrity failure");
    } else if (result == FR_LOCKED || result == FR_TOO_MANY_OPEN_FILES) fail("filesystem object busy");
    else if (result == FR_EXIST) fail("destination already exists");
    else if (result == FR_DENIED) fail("filesystem access denied or full");
    else fail("filesystem path/operation failed");
    return false;
}
/* Bounded absolute UTF-8 paths, no drive syntax, traversal or empty components. */
static bool valid_path(const char *path) {
    if (!path || path[0] != '/') return false;
    size_t count = 0, component = 0, depth = 0;
    for (const char *p = path + 1; ; ++p) {
        if (++count >= RISC_STORAGE_VOLUME_PATH_MAX) return false;
        const unsigned char c = (unsigned char)*p;
        if (!c || c == '/') {
            if (!component) return !c && p == path + 1;
            const char *start = p - component;
            if ((component == 1 && start[0] == '.') ||
                (component == 2 && start[0] == '.' && start[1] == '.') ||
                start[component - 1] == '.' || start[component - 1] == ' ' || ++depth > 24u) return false;
            if (!c) return true;
            component = 0;
        } else {
            if (c < 32u || c == ':' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
            ++component;
        }
    }
}
static uint32_t allocate_handle(unsigned slot) {
    if (next_generation >= 0xffffffu) { fail("handle generation exhausted"); return 0; }
    return (++next_generation << 8) | (slot + 1u);
}
static file_slot *file_for(uint32_t handle) {
    const unsigned slot = (handle & 255u);
    return slot && slot <= FILE_SLOTS && files[slot - 1].handle == handle ? &files[slot - 1] : 0;
}
static dir_slot *dir_for(uint32_t handle) {
    const unsigned slot = (handle & 255u);
    return slot && slot <= DIR_SLOTS && dirs[slot - 1].handle == handle ? &dirs[slot - 1] : 0;
}
static bool mount_filesystem(void) {
    mounted = result_ok(f_mount(&filesystem, "", 1));
    return mounted;
}
static bool refresh(void *context) {
    (void)context;
    if (!enter()) return false;
    if (has_handles()) { fail("remount refused with live handles"); leave(); return false; }
    (void)f_mount(0, "", 0);
    mounted = card_ready = io_failed = false;
    error[0] = 0;
    if (started) (void)init_card();
    leave();
    return started;
}
static bool ready(void *context) { (void)context; return mounted && !io_failed && !__atomic_load_n(&power_down_prepared, __ATOMIC_ACQUIRE); }
static bool label(void *context, char *out, size_t size) {
    (void)context;
    if (!ready(0) || !out || size < sizeof(STORAGE_VOLUME_LABEL)) return false;
    memcpy(out, STORAGE_VOLUME_LABEL, sizeof(STORAGE_VOLUME_LABEL)); return true;
}
static bool stat_path(void *context, const char *path, uint64_t *size, bool *directory) {
    (void)context;
    if (!ready(0) || !valid_path(path) || !size || !directory || !enter()) return false;
    FILINFO info;
    FRESULT result = FR_OK;
    if (equal(path, "/")) { memset(&info, 0, sizeof(info)); info.fattrib = AM_DIR; }
    else result = f_stat(path, &info);
    if (result == FR_OK) { *size = info.fsize; *directory = (info.fattrib & AM_DIR) != 0; }
    bool ok = result_ok(result); leave(); return ok;
}
static risc_storage_dir_t dir_open(void *context, const char *path) {
    (void)context;
    if (!ready(0) || !valid_path(path) || !enter()) return 0;
    uint32_t handle = 0;
    for (unsigned i = 0; i < DIR_SLOTS; ++i) if (!dirs[i].handle) {
        const uint32_t candidate = allocate_handle(i);
        if (!candidate) break;
        if (result_ok(f_opendir(&dirs[i].object, path))) {
            dirs[i].error = 0; dirs[i].handle = handle = candidate;
            memcpy(dirs[i].path, path, strlen(path) + 1);
        }
        break;
    }
    leave(); return handle;
}
static bool dir_next(void *context, risc_storage_dir_t handle, risc_storage_dirent_v1 *out) {
    (void)context;
    if (!out || !ready(0) || !enter()) return false;
    dir_slot *slot = dir_for(handle);
    bool ok = false;
    if (slot && !slot->error) {
        FILINFO info;
        const FRESULT result = f_readdir(&slot->object, &info);
        if (result_ok(result)) {
            const size_t length = strlen(info.fname);
            if (length && length < sizeof(out->name)) {
                memset(out, 0, sizeof(*out)); memcpy(out->name, info.fname, length + 1);
                out->size = info.fsize; out->is_directory = !!(info.fattrib & AM_DIR); ok = true;
            } else if (length) slot->error = FR_INVALID_NAME;
        } else slot->error = result;
    }
    leave(); return ok;
}
static bool dir_rewind(void *context, risc_storage_dir_t handle) {
    (void)context; if (!ready(0) || !enter()) return false;
    dir_slot *slot = dir_for(handle);
    const bool ok = slot && result_ok(f_readdir(&slot->object, 0));
    if (ok) slot->error = 0;
    leave(); return ok;
}
static bool dir_close_checked(void *context, risc_storage_dir_t handle) {
    (void)context; if (!enter()) return false;
    dir_slot *slot = dir_for(handle);
    const bool ok = slot && (io_failed || result_ok(f_closedir(&slot->object)));
    if (ok) slot->handle = 0;
    leave(); return ok;
}
static void dir_close(void *context, risc_storage_dir_t handle) { (void)dir_close_checked(context, handle); }
static risc_storage_file_t file_open(void *context, const char *path, uint32_t flags) {
    (void)context;
    if (!ready(0) || !valid_path(path) || !(flags & 3u) || (flags & ~63u) ||
        ((flags & 60u) && !(flags & RISC_STORAGE_OPEN_WRITE)) ||
        ((flags & RISC_STORAGE_OPEN_EXCLUSIVE) && !(flags & RISC_STORAGE_OPEN_CREATE)) || !enter()) return 0;
    BYTE mode = (flags & RISC_STORAGE_OPEN_READ ? FA_READ : 0) | (flags & RISC_STORAGE_OPEN_WRITE ? FA_WRITE : 0);
    if (flags & RISC_STORAGE_OPEN_EXCLUSIVE) mode |= FA_CREATE_NEW;
    else if (flags & RISC_STORAGE_OPEN_CREATE) mode |= flags & RISC_STORAGE_OPEN_TRUNCATE ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    uint32_t handle = 0;
    for (unsigned i = 0; i < FILE_SLOTS; ++i) if (!files[i].handle) {
        const uint32_t candidate = allocate_handle(i);
        if (!candidate) break;
        file_slot *slot = &files[i];
        if (!result_ok(f_open(&slot->object, path, mode))) break;
        slot->handle = candidate; slot->flags = flags; slot->error = 0; slot->abortable = false;
        memcpy(slot->path, path, strlen(path) + 1);
        if ((flags & RISC_STORAGE_OPEN_TRUNCATE) && !(flags & RISC_STORAGE_OPEN_CREATE)) {
            if (!result_ok(f_truncate(&slot->object))) { slot->error = FR_DISK_ERR; break; }
        }
        if ((flags & RISC_STORAGE_OPEN_APPEND) && !result_ok(f_lseek(&slot->object, f_size(&slot->object)))) {
            slot->error = FR_DISK_ERR; break;
        }
        handle = candidate; break;
    }
    leave(); return handle;
}
static risc_storage_file_t file_open_read(void *context, const char *path, uint64_t *size) {
    if (!size) return 0;
    const uint32_t handle = file_open(context, path, RISC_STORAGE_OPEN_READ);
    file_slot *slot = file_for(handle);
    if (slot) *size = f_size(&slot->object);
    return handle;
}
static risc_storage_file_t file_open_write(void *context, const char *path) {
    const uint32_t handle = file_open(context, path, RISC_STORAGE_OPEN_WRITE | RISC_STORAGE_OPEN_CREATE | RISC_STORAGE_OPEN_EXCLUSIVE);
    file_slot *slot = file_for(handle); if (slot) slot->abortable = true;
    return handle;
}
static size_t file_read(void *context, uint32_t handle, void *buffer, size_t capacity) {
    (void)context;
    if (!ready(0) || !buffer || !capacity || !enter()) return 0;
    file_slot *slot = file_for(handle); UINT count = 0;
    if (slot && !slot->error && (slot->flags & RISC_STORAGE_OPEN_READ)) {
        const FRESULT result = f_read(&slot->object, buffer, capacity < RISC_STORAGE_VOLUME_IO_MAX ? capacity : RISC_STORAGE_VOLUME_IO_MAX, &count);
        if (!result_ok(result)) slot->error = result;
    } else if (slot && !slot->error) slot->error = FR_DENIED;
    leave(); return count;
}
static size_t file_write(void *context, uint32_t handle, const void *buffer, size_t size) {
    (void)context;
    if (!ready(0) || !buffer || !size || !enter()) return 0;
    file_slot *slot = file_for(handle); UINT count = 0;
    if (slot && !slot->error && (slot->flags & RISC_STORAGE_OPEN_WRITE)) {
        FRESULT result = FR_OK;
        if (slot->flags & RISC_STORAGE_OPEN_APPEND) result = f_lseek(&slot->object, f_size(&slot->object));
        const UINT request = size < RISC_STORAGE_VOLUME_IO_MAX ? size : RISC_STORAGE_VOLUME_IO_MAX;
        if (result == FR_OK) result = f_write(&slot->object, buffer, request, &count);
        if (!result_ok(result)) slot->error = result;
        else if (count != request) { slot->error = FR_DENIED; fail("volume full"); }
    } else if (slot && !slot->error) slot->error = FR_DENIED;
    leave(); return count;
}
static bool file_seek(void *context, uint32_t handle, uint64_t offset) {
    (void)context; if (!ready(0) || offset > UINT32_MAX || !enter()) return false;
    file_slot *slot = file_for(handle);
    bool ok = false;
    /* Do not create uninitialized holes. Callers extend with explicit writes. */
    if (slot && !slot->error && offset <= f_size(&slot->object)) {
        const FRESULT result = f_lseek(&slot->object, (FSIZE_t)offset);
        ok = result_ok(result) && f_tell(&slot->object) == offset;
        if (!ok) slot->error = result == FR_OK ? FR_INT_ERR : result;
    }
    leave(); return ok;
}
static bool file_info(void *context, uint32_t handle, uint64_t *size, uint64_t *position) {
    (void)context; if (!ready(0) || !size || !position || !enter()) return false;
    file_slot *slot = file_for(handle);
    if (slot) { *size = f_size(&slot->object); *position = f_tell(&slot->object); }
    leave(); return slot != 0;
}
static bool file_sync(void *context, uint32_t handle) {
    (void)context; if (!ready(0) || !enter()) return false;
    file_slot *slot = file_for(handle);
    bool ok = false;
    if (slot && !slot->error) {
        FRESULT result = f_sync(&slot->object); ok = result_ok(result); if (!ok) slot->error = result;
    }
    leave(); return ok;
}
static bool file_close(void *context, uint32_t handle, bool commit) {
    (void)context; if (!enter()) return false;
    file_slot *slot = file_for(handle);
    bool ok = false;
    if (slot) {
        if (io_failed && (slot->flags & RISC_STORAGE_OPEN_WRITE)) {
            /* Retain uncertain writable ownership; quiesce/remount must refuse. */
            leave(); return false;
        }
        ok = io_failed || result_ok(f_close(&slot->object));
        if (ok && slot->abortable && !commit && !io_failed) ok = result_ok(f_unlink(slot->path));
        if (ok) slot->handle = 0;
    }
    leave(); return ok;
}
static uint32_t handle_error(void *context, uint32_t handle, bool directory) {
    (void)context;
    if (!enter()) return FR_LOCKED;
    const dir_slot *dir = directory ? dir_for(handle) : 0;
    const file_slot *file = directory ? 0 : file_for(handle);
    uint32_t result = !ready(0) ? FR_DISK_ERR : dir ? dir->error : file ? file->error : FR_INVALID_OBJECT;
    leave(); return result;
}
/* Compare actual directory clusters rather than textual prefixes: SFN aliases
 * and case variants must not let a rename invalidate a live descendant. */
static bool path_uses_cluster(const char *path, bool leaf_directory, DWORD cluster) {
    char prefix[RISC_STORAGE_VOLUME_PATH_MAX];
    memcpy(prefix, path, strlen(path) + 1);
    const size_t length = strlen(prefix);
    for (size_t i = 1; i <= length; ++i) {
        if (prefix[i] != '/' && !(i == length && leaf_directory)) continue;
        const char saved = prefix[i]; prefix[i] = 0;
        DIR probe;
        FRESULT result = f_opendir(&probe, prefix);
        if (result != FR_OK) { (void)result_ok(result); return true; }
        const bool match = probe.obj.sclust == cluster;
        result = f_closedir(&probe);
        prefix[i] = saved;
        if (!result_ok(result) || match) return true;
    }
    return false;
}
static bool directory_in_use(const char *path) {
    FILINFO info;
    if (f_stat(path, &info) != FR_OK || !(info.fattrib & AM_DIR)) return false;
    DIR probe;
    FRESULT result = f_opendir(&probe, path);
    if (!result_ok(result)) return true;
    const DWORD cluster = probe.obj.sclust;
    if (!result_ok(f_closedir(&probe))) return true;
    for (unsigned i = 0; i < FILE_SLOTS; ++i)
        if (files[i].handle && path_uses_cluster(files[i].path, false, cluster)) return true;
    for (unsigned i = 0; i < DIR_SLOTS; ++i)
        if (dirs[i].handle && path_uses_cluster(dirs[i].path, true, cluster)) return true;
    return false;
}
static bool remove_path(void *context, const char *path) {
    (void)context; if (!ready(0) || !valid_path(path) || equal(path, "/") || !enter()) return false;
    if (directory_in_use(path)) { fail("directory has live handles"); leave(); return false; }
    bool ok = result_ok(f_unlink(path)); leave(); return ok;
}
static bool mkdir_path(void *context, const char *path) {
    (void)context; if (!ready(0) || !valid_path(path) || !enter()) return false;
    bool ok = result_ok(f_mkdir(path)); leave(); return ok;
}
static bool rename_path(void *context, const char *from, const char *to) {
    (void)context; if (!ready(0) || !valid_path(from) || !valid_path(to) || equal(from, "/") || equal(to, "/") || !enter()) return false;
    FILINFO info;
    FRESULT destination = f_stat(to, &info);
    if (destination == FR_OK) { fail("destination already exists"); leave(); return false; }
    if (destination != FR_NO_FILE && destination != FR_NO_PATH) {
        (void)result_ok(destination); leave(); return false;
    }
    if (!result_ok(f_stat(from, &info))) { leave(); return false; }
    if (info.fattrib & AM_DIR) {
        DIR probe;
        if (!result_ok(f_opendir(&probe, from))) { leave(); return false; }
        DWORD cluster = probe.obj.sclust;
        if (!result_ok(f_closedir(&probe)) || path_uses_cluster(to, false, cluster)) {
            fail("cannot move directory into itself"); leave(); return false;
        }
    }
    if (directory_in_use(from)) { fail("rename has live descendants"); leave(); return false; }
    bool ok = result_ok(f_rename(from, to)); leave(); return ok;
}
static bool last_error_api(void *context, char *out, size_t capacity) {
    (void)context; if (!out || !capacity) return false;
    size_t i = 0; while (error[i] && i + 1 < capacity) { out[i] = error[i]; ++i; }
    out[i] = 0; return error[0] != 0;
}
static bool prepare_power_down(void *context) {
    (void)context;
    if (!enter_lifecycle()) return false;
    if (power_down_prepared) { leave(); return true; }
    if (io_failed || !transport_idle()) { leave(); return false; }
    for (unsigned i = 0; i < FILE_SLOTS; ++i) {
        if (files[i].handle && (files[i].flags & RISC_STORAGE_OPEN_WRITE)) {
            fail("power down refused with live writer"); leave(); return false;
        }
    }
    // Read handles carry only RAM metadata; keep their ELF and dependencies
    // pinned. No filesystem operation or active transport survives this barrier.
    if (card_ready && !sync_card()) { fail("power down media sync failed"); leave(); return false; }
    __atomic_store_n(&power_down_prepared, true, __ATOMIC_RELEASE);
    leave(); return true;
}
static bool cancel_power_down(void *context) {
    (void)context;
    if (!enter_lifecycle()) return false;
    const bool safe = !io_failed && transport_idle();
    if (safe) __atomic_store_n(&power_down_prepared, false, __ATOMIC_RELEASE);
    leave(); return safe;
}
static const risc_storage_volume_api_v1_power api = {
  {
    {RISC_STORAGE_VOLUME_API_V1, sizeof(api), 0, refresh, ready, label, stat_path,
     dir_open, dir_next, dir_close, file_open_read, file_read, file_open_write,
     file_write, file_close, remove_path, last_error_api},
    file_open, file_seek, file_info, file_sync, dir_rewind, dir_close_checked,
    handle_error, mkdir_path, rename_path
  }, prepare_power_down, cancel_power_down
};
