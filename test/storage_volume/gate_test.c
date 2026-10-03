/* Real X4 provider + FatFs, with only the physical pins, clock and OS boundary
 * replaced. Each poisoned generation runs in its own process, as on reboot. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "os_cpu_fake.h"
#include "../../Drivers/x4pro_sd/driver.c"
static unsigned pin_calls, rail_commits;
static uint64_t now_ms;
static bool reenter, in_reentry, shift_task, shift_isr;
static bool block_sleep, sleep_blocked, resume_sleep, refresh_result;
static pthread_mutex_t barrier = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static void rejected_calls(void) {
    const unsigned pins_before = pin_calls;
    char text[80] = "untouched", byte = 0;
    uint64_t size = 0, position = 0; bool directory = false;
    risc_storage_dirent_v1 entry;
    assert(!refresh(0) && !ready(0) && !label(0, text, sizeof(text)));
    assert(!stat_path(0, "/", &size, &directory));
    assert(!dir_open(0, "/") && !dir_next(0, 1, &entry) && !dir_rewind(0, 1));
    assert(!dir_close_checked(0, 1)); dir_close(0, 1);
    assert(!file_open_read(0, "/read", &size) && !file_open_write(0, "/write"));
    assert(!file_read(0, 1, &byte, 1) && !file_write(0, 1, &byte, 1));
    assert(!file_seek(0, 1, 0) && !file_info(0, 1, &size, &position) && !file_sync(0, 1));
    assert(!file_close(0, 1, true));
    assert(handle_error(0, 1, false) == FR_LOCKED);
    assert(!remove_path(0, "/a") && !mkdir_path(0, "/a") && !rename_path(0, "/a", "/b"));
    assert(!prepare_power_down(0) && !cancel_power_down(0) && !commit_power_down(0));
    assert(!last_error_api(0, text, sizeof(text)) && !strcmp(text, "untouched"));
    assert(!quiesce()); stop();
    assert(pin_calls == pins_before && !sd_mutex_deletes);
}
static void record_pin(void) { ++pin_calls; }
void x4pro_pin_output(uint32_t pin, bool level) { (void)pin; (void)level; record_pin(); }
void x4pro_pin_release(uint32_t pin) { (void)pin; record_pin(); }
bool x4pro_pin_read(uint32_t pin) { (void)pin; record_pin(); return true; }
void x4pro_pin_input(uint32_t pin, bool pullup) { (void)pin; (void)pullup; record_pin(); }
void x4pro_pin_hold(uint32_t pin, bool hold) { (void)pin; if (hold) ++rail_commits; record_pin(); }
static uint64_t monotonic(void *context) { (void)context; return now_ms; }
static void sleep_ms(void *context, uint32_t ms) {
    (void)context; now_ms += ms;
    assert(operation_mutex && sd_mutex_creates > sd_mutex_deletes);
    if (shift_task || shift_isr) sd_mutex_set_context(shift_isr, false, shift_task);
    if (reenter && !in_reentry) { in_reentry = true; rejected_calls(); in_reentry = false; }
    if (block_sleep) {
        assert(!pthread_mutex_lock(&barrier));
        sleep_blocked = true; assert(!pthread_cond_broadcast(&condition));
        while (!resume_sleep) assert(!pthread_cond_wait(&condition, &barrier));
        assert(!pthread_mutex_unlock(&barrier));
    }
}
static const risc_platform_clock_api_v1 fixture_clock = {1, sizeof(fixture_clock), 0, monotonic, sleep_ms};
static const risc_provider_dependency_v1 dep = {"platform.clock", 1, &fixture_clock};
static void pause_give(void) {
    assert(!pthread_mutex_lock(&barrier));
    sleep_blocked = true; assert(!pthread_cond_broadcast(&condition));
    while (!resume_sleep) assert(!pthread_cond_wait(&condition, &barrier));
    assert(!pthread_mutex_unlock(&barrier));
}
static void *quiesce_thread(void *unused) { (void)unused; refresh_result = quiesce(); return 0; }
static void *refresh_thread(void *unused) { (void)unused; refresh_result = refresh(0); return 0; }
static void check_poison(void) {
    assert(mutex_poisoned && operation_mutex && !sd_mutex_deletes);
    sd_mutex_fail_give = false; sd_mutex_set_context(false, false, false);
    const unsigned pins_before = pin_calls, takes = sd_mutex_takes;
    rejected_calls(); assert(!start(&dep, 1));
    assert(pin_calls == pins_before && sd_mutex_takes == takes && !sd_mutex_deletes);
}
int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "lifetime";
    sd_mutex_set_context(true, false, false); assert(!start(&dep, 1));
    sd_mutex_set_context(false, true, false); assert(!start(&dep, 1));
    sd_mutex_set_context(false, false, false);
    sd_mutex_fail_create = true; assert(!start(&dep, 1));
    assert(!pin_calls && !sd_mutex_creates); sd_mutex_fail_create = false;
    if (!strcmp(scenario, "start-give")) {
        sd_mutex_fail_give = true; assert(!start(&dep, 1)); check_poison(); goto done;
    }
    if (!strcmp(scenario, "non-owner") || !strcmp(scenario, "isr-leave")) {
        shift_task = !strcmp(scenario, "non-owner"); shift_isr = !shift_task;
        assert(!start(&dep, 1)); check_poison(); goto done;
    }
    assert(start(&dep, 1));
    if (!strcmp(scenario, "lifetime")) {
        reenter = true; assert(refresh(0)); reenter = false;
        block_sleep = true; pthread_t worker;
        assert(!pthread_create(&worker, 0, refresh_thread, 0));
        assert(!pthread_mutex_lock(&barrier));
        while (!sleep_blocked) assert(!pthread_cond_wait(&condition, &barrier));
        assert(!pthread_mutex_unlock(&barrier));
        rejected_calls();
        assert(!pthread_mutex_lock(&barrier)); resume_sleep = true;
        assert(!pthread_cond_broadcast(&condition)); assert(!pthread_mutex_unlock(&barrier));
        assert(!pthread_join(worker, 0) && refresh_result); block_sleep = false;
        sd_mutex_set_context(true, false, false); rejected_calls();
        sd_mutex_set_context(false, true, false); rejected_calls();
        sd_mutex_set_context(false, false, false);
        sd_mutex_fail_take = true; rejected_calls(); sd_mutex_fail_take = false;
        assert(quiesce());
        const unsigned takes = sd_mutex_takes, gives = sd_mutex_gives, pins = pin_calls;
        sd_mutex_fail_take = sd_mutex_fail_give = true;
        assert(quiesce()); stop(); stop();
        assert(sd_mutex_deletes == 1 && sd_mutex_takes == takes && sd_mutex_gives == gives && pin_calls == pins);
        sd_mutex_fail_take = sd_mutex_fail_give = false;
        assert(start(&dep, 1) && sd_mutex_creates == 2); assert(quiesce()); stop();
        assert(sd_mutex_deletes == 2); goto done;
    }
    if (!strcmp(scenario, "quiesce-race") || !strcmp(scenario, "quiesce-give")) {
        sd_mutex_fail_give = !strcmp(scenario, "quiesce-give");
        sd_mutex_before_give = pause_give;
        pthread_t worker; assert(!pthread_create(&worker, 0, quiesce_thread, 0));
        assert(!pthread_mutex_lock(&barrier));
        while (!sleep_blocked) assert(!pthread_cond_wait(&condition, &barrier));
        assert(!pthread_mutex_unlock(&barrier));
        assert(quiescing && !quiesced); rejected_calls();
        assert(!pthread_mutex_lock(&barrier)); resume_sleep = true;
        assert(!pthread_cond_broadcast(&condition)); assert(!pthread_mutex_unlock(&barrier));
        assert(!pthread_join(worker, 0)); sd_mutex_before_give = 0;
        if (sd_mutex_fail_give) {
            assert(!refresh_result && !quiesced); check_poison();
        } else {
            assert(refresh_result && quiesced);
            const unsigned takes = sd_mutex_takes, gives = sd_mutex_gives;
            sd_mutex_fail_take = sd_mutex_fail_give = true; stop();
            assert(sd_mutex_deletes == 1 && sd_mutex_takes == takes && sd_mutex_gives == gives);
        }
        goto done;
    }
    if (!strcmp(scenario, "prepare-give")) {
        sd_mutex_fail_give = true; assert(!prepare_power_down(0)); check_poison(); goto done;
    }
    assert(prepare_power_down(0));
    if (!strcmp(scenario, "cancel-give")) {
        sd_mutex_fail_give = true; assert(!cancel_power_down(0)); check_poison(); goto done;
    }
    if (!strcmp(scenario, "commit-give")) {
        sd_mutex_fail_give = true; assert(!commit_power_down(0));
        assert(rail_commits == 1); check_poison(); goto done;
    }
    if (!strcmp(scenario, "repeat-prepare-give")) {
        sd_mutex_fail_give = true; assert(!prepare_power_down(0)); check_poison(); goto done;
    }
    if (!strcmp(scenario, "repeat-commit-give")) {
        assert(commit_power_down(0) && rail_commits == 1);
        sd_mutex_fail_give = true; assert(!commit_power_down(0));
        assert(rail_commits == 1); check_poison(); goto done;
    }
    assert(!"unknown scenario");
done:
    printf("X4 storage admission: %s PASS\n", scenario); return 0;
}
