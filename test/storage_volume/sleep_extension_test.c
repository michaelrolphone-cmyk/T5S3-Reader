/* Additive suffix probing must never reinterpret a legacy provider as a
 * resumable transaction, even when it supplies the terminal commit prefix. */
#include <RiscStorageVolumeV1.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool phase(void *context) { (void)context; return true; }
static int32_t resume(void *context) { (void)context; return RISC_STORAGE_SLEEP_READY; }

_Static_assert(offsetof(risc_storage_volume_api_v1_sleep, terminal) == 0,
               "terminal prefix moved");
_Static_assert(offsetof(risc_storage_volume_api_v1_sleep, sleep_tag) ==
               sizeof(risc_storage_volume_api_v1_power_commit), "suffix overlap");

int main(void) {
    risc_storage_volume_api_v1_sleep api = {0};
    risc_storage_volume_api_v1 *base = &api.terminal.power.volume.base;
    base->api_version = RISC_STORAGE_VOLUME_API_V1;
    base->struct_size = sizeof(api);
    api.terminal.extension_tag = RISC_STORAGE_POWER_COMMIT_TAG;
    api.terminal.extension_version = 1;
    api.terminal.commit_power_down = phase;
    api.sleep_tag = RISC_STORAGE_SLEEP_TAG;
    api.sleep_version = 1;
    api.prepare_sleep = phase;
    api.commit_sleep = phase;
    api.resume_sleep = resume;
    assert(risc_storage_volume_sleep(base) == &api);
    assert(risc_storage_volume_power_commit(base) == &api.terminal);
    assert(risc_storage_volume_power(base) == &api.terminal.power);
    assert(risc_storage_volume_extension(base) == &api.terminal.power.volume);
    for (size_t size = 0; size < sizeof(api); ++size) {
        base->struct_size = (uint32_t)size;
        assert(!risc_storage_volume_sleep(base));
    }
    base->struct_size = sizeof(api);
    api.sleep_tag ^= 1; assert(!risc_storage_volume_sleep(base)); api.sleep_tag ^= 1;
    api.sleep_version = 2; assert(!risc_storage_volume_sleep(base)); api.sleep_version = 1;
    api.prepare_sleep = NULL; assert(!risc_storage_volume_sleep(base)); api.prepare_sleep = phase;
    api.commit_sleep = NULL; assert(!risc_storage_volume_sleep(base)); api.commit_sleep = phase;
    api.resume_sleep = NULL; assert(!risc_storage_volume_sleep(base)); api.resume_sleep = resume;
    api.terminal.extension_tag = 0; assert(!risc_storage_volume_sleep(base));
    api.terminal.extension_tag = RISC_STORAGE_POWER_COMMIT_TAG;
    base->api_version = 2; assert(!risc_storage_volume_sleep(base)); base->api_version = 1;
    assert(!risc_storage_volume_sleep(NULL));
    risc_storage_volume_api_v1_power_commit legacy;
    memcpy(&legacy, &api.terminal, sizeof(legacy));
    legacy.power.volume.base.struct_size = sizeof(legacy);
    assert(risc_storage_volume_power_commit(&legacy.power.volume.base) == &legacy);
    assert(!risc_storage_volume_sleep(&legacy.power.volume.base));
    puts("storage sleep extension prefix/probe PASS");
    return 0;
}
