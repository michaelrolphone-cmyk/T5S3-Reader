/* Real SD provider + FatFs, with only the transport, clock and OS boundary
 * replaced. Each poisoned generation runs in its own process, as on reboot. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "os_cpu_fake.h"
#ifdef TEST_SPI_TRANSPORT
#include "../../Drivers/t5s3_sd/driver.c"
#define TRANSPORT_NAME "T5 SPI"
#else
#include "../../Drivers/x4pro_sd/driver.c"
#define TRANSPORT_NAME "X4"
#endif
static unsigned hardware_calls;
#ifndef TEST_SPI_TRANSPORT
static unsigned rail_commits;
#endif
static uint64_t now_ms;
uint32_t x4pro_sd_test_cycle_count(void) { static uint32_t cycles; return cycles += 8; }
static bool reenter, in_reentry, shift_task, shift_isr;
static bool block_sleep, sleep_blocked, resume_sleep, refresh_result;
static pthread_mutex_t barrier = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool commit_power_down_test(void) {
    const risc_storage_volume_api_v1_power_commit *commit =
        risc_storage_volume_power_commit((const risc_storage_volume_api_v1 *)&api);
    return commit && commit->commit_power_down(0);
}
static void rejected_calls(void) {
    const unsigned hardware_before = hardware_calls;
    char text[80] = "untouched", byte = 0;
    uint64_t size = 0, position = 0; bool directory = false;
    risc_storage_dirent_v1 entry;
    assert(!refresh(0) && !ready(0) && !label(0, text, sizeof(text)));
    assert(!stat_path(0, "/", &size, &directory));
    assert(!dir_open(0, "/") && !dir_next(0, 1, &entry) && !dir_rewind(0, 1));
    assert(!dir_close_checked(0, 1)); dir_close(0, 1);
    assert(!file_open_read(0, "/read", &size) && !file_open_write(0, "/write") &&
           !file_open(0, "/read", RISC_STORAGE_OPEN_READ));
    assert(!file_read(0, 1, &byte, 1) && !file_write(0, 1, &byte, 1));
    assert(!file_seek(0, 1, 0) && !file_info(0, 1, &size, &position) && !file_sync(0, 1));
    assert(!file_close(0, 1, true));
    assert(handle_error(0, 1, false) == FR_LOCKED);
    assert(!remove_path(0, "/a") && !mkdir_path(0, "/a") && !rename_path(0, "/a", "/b"));
    assert(!prepare_power_down(0) && !cancel_power_down(0) && !commit_power_down_test());
    assert(!last_error_api(0, text, sizeof(text)) && !strcmp(text, "untouched"));
    assert(!quiesce()); stop();
    assert(hardware_calls == hardware_before && !sd_mutex_deletes);
}
#ifndef TEST_SPI_TRANSPORT
static void record_pin(void) { ++hardware_calls; }
void x4pro_pin_output(uint32_t pin, bool level) { (void)pin; (void)level; record_pin(); }
void x4pro_pin_level(uint32_t pin, bool level) { (void)pin; (void)level; record_pin(); }
void x4pro_pin_release(uint32_t pin) { (void)pin; record_pin(); }
bool x4pro_pin_read(uint32_t pin) { (void)pin; record_pin(); return true; }
void x4pro_pin_input(uint32_t pin, bool pullup) { (void)pin; (void)pullup; record_pin(); }
void x4pro_pin_hold(uint32_t pin, bool hold) { (void)pin; if (hold) ++rail_commits; record_pin(); }
#else
/* Gate tests need only an absent card. All ownership still crosses the real
 * SPI provider ABI, with deliberately nontrivial generation tokens. */
static bool fixture_claimed, fixture_active, fail_release, fail_end;
static uint64_t fixture_claim, fixture_session, fixture_generation = 0x1234567800ULL;
static uint64_t last_release;
static unsigned claims, releases, ends;
static bool fixture_claim_device(void *context, uint8_t cs, uint64_t *out) {
    (void)context; ++hardware_calls; ++claims;
    assert(operation_mutex && sd_mutex_creates > sd_mutex_deletes);
    assert(cs == 12 && !fixture_claimed && !fixture_active && out);
    fixture_claimed = true; *out = fixture_claim = ++fixture_generation; return true;
}
static bool fixture_begin(void *context, uint64_t claim, uint32_t hz, bool selected, uint64_t *out) {
    (void)context; (void)selected; ++hardware_calls;
    assert(fixture_claimed && claim == fixture_claim && !fixture_active && out);
    assert(hz == 400000 || hz == 25000000);
    fixture_active = true; *out = fixture_session = ++fixture_generation; return true;
}
static bool fixture_select(void *context, uint64_t session, bool selected) {
    (void)context; (void)selected; ++hardware_calls;
    assert(fixture_active && session == fixture_session); return true;
}
static bool fixture_transfer(void *context, uint64_t session, const uint8_t *tx, uint8_t *rx, size_t bytes) {
    (void)context; (void)tx; ++hardware_calls;
    assert(fixture_active && session == fixture_session && bytes && bytes <= RISC_SPI_TRANSFER_MAX);
    if (rx) memset(rx, 0xff, bytes);
    return true;
}
static bool fixture_end(void *context, uint64_t session) {
    (void)context; ++hardware_calls; ++ends;
    assert(fixture_active && session == fixture_session);
    if (fail_end) return false;
    fixture_active = false; fixture_session = 0; return true;
}
static bool fixture_release(void *context, uint64_t claim) {
    (void)context; ++hardware_calls; ++releases; last_release = claim;
    assert(fixture_claimed && claim == fixture_claim && !fixture_active);
    if (fail_release) return false;
    fixture_claimed = false; fixture_claim = 0; return true;
}
static const risc_spi_bus_api_v1 fixture_spi = {
    1, sizeof(fixture_spi), 0, fixture_claim_device, fixture_begin, fixture_select,
    fixture_transfer, fixture_end, fixture_release
};
#endif
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
static const risc_provider_dependency_v1 deps[] = {{"platform.clock", 1, &fixture_clock}
#ifdef TEST_SPI_TRANSPORT
    , {"spi.bus", 1, &fixture_spi}
#endif
};
#define START() start(deps, sizeof(deps) / sizeof(deps[0]))
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
    const unsigned hardware_before = hardware_calls, takes = sd_mutex_takes;
#ifdef TEST_SPI_TRANSPORT
    const uint64_t claim = bus_claim, session = bus_session;
    assert(fixture_claimed == (claim != 0) && fixture_active == (session != 0));
#endif
    rejected_calls(); assert(!START());
    assert(hardware_calls == hardware_before && sd_mutex_takes == takes && !sd_mutex_deletes);
#ifdef TEST_SPI_TRANSPORT
    assert(bus_claim == claim && bus_session == session);
    assert(fixture_claim == claim && fixture_session == session);
#endif
}
int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "lifetime";
    sd_mutex_set_context(true, false, false); assert(!START());
    sd_mutex_set_context(false, true, false); assert(!START());
    sd_mutex_set_context(false, false, false);
    sd_mutex_fail_create = true; assert(!START());
    assert(!hardware_calls && !sd_mutex_creates); sd_mutex_fail_create = false;
    if (!strcmp(scenario, "start-take")) {
        sd_mutex_fail_take = true; assert(!START());
        assert(!hardware_calls && sd_mutex_creates == 1 && !sd_mutex_deletes);
        sd_mutex_fail_take = false; assert(quiesce()); stop();
        assert(sd_mutex_deletes == 1); assert(START());
        assert(sd_mutex_creates == 2); assert(quiesce()); stop();
        assert(sd_mutex_deletes == 2); goto done;
    }
    if (!strcmp(scenario, "start-give")) {
        sd_mutex_fail_give = true; assert(!START()); check_poison(); goto done;
    }
    if (!strcmp(scenario, "non-owner") || !strcmp(scenario, "isr-leave")) {
        shift_task = !strcmp(scenario, "non-owner"); shift_isr = !shift_task;
        assert(!START()); check_poison(); goto done;
    }
    assert(START());
#ifdef TEST_SPI_TRANSPORT
    assert(!risc_storage_volume_power_commit((const risc_storage_volume_api_v1 *)&api));
    if (!strcmp(scenario, "release-retained")) {
        const uint64_t claim = bus_claim;
        const unsigned claims_before = claims;
        assert(claim && claim == fixture_claim && !bus_session);
        fail_release = true; assert(!quiesce()); stop();
        assert(bus_claim == claim && fixture_claim == claim && last_release == claim);
        assert(started && !quiesced && !sd_mutex_deletes && releases == 2);
        assert(!START() && claims == claims_before && bus_claim == claim);
        fail_release = false; assert(quiesce()); stop();
        assert(!bus_claim && !fixture_claimed && sd_mutex_deletes == 1);
        assert(releases == 3 && last_release == claim);
        assert(START() && bus_claim && bus_claim != claim);
        assert(quiesce()); stop(); assert(sd_mutex_deletes == 2); goto done;
    }
    if (!strcmp(scenario, "end-retained")) {
        const uint64_t claim = bus_claim;
        fail_end = true; assert(refresh(0));
        const uint64_t session = bus_session;
        assert(session && session == fixture_session && bus_claim == claim);
        const unsigned hardware_before = hardware_calls, ends_before = ends;
        assert(!prepare_power_down(0) && !cancel_power_down(0));
        assert(!quiesce()); stop(); assert(!START());
        fail_end = false; assert(!quiesce()); stop();
        assert(bus_claim == claim && bus_session == session && fixture_active);
        assert(hardware_calls == hardware_before && ends == ends_before && !releases && !sd_mutex_deletes);
        goto done;
    }
#endif
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
        const unsigned takes = sd_mutex_takes, gives = sd_mutex_gives, pins = hardware_calls;
        sd_mutex_fail_take = sd_mutex_fail_give = true;
        assert(quiesce()); stop(); stop();
        assert(sd_mutex_deletes == 1 && sd_mutex_takes == takes && sd_mutex_gives == gives && hardware_calls == pins);
        sd_mutex_fail_take = sd_mutex_fail_give = false;
        assert(START() && sd_mutex_creates == 2); assert(quiesce()); stop();
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
#ifndef TEST_SPI_TRANSPORT
    if (!strcmp(scenario, "commit-give")) {
        sd_mutex_fail_give = true; assert(!commit_power_down_test());
        assert(rail_commits == 1); check_poison(); goto done;
    }
#endif
    if (!strcmp(scenario, "repeat-prepare-give")) {
        sd_mutex_fail_give = true; assert(!prepare_power_down(0)); check_poison(); goto done;
    }
#ifndef TEST_SPI_TRANSPORT
    if (!strcmp(scenario, "repeat-commit-give")) {
        assert(commit_power_down_test() && rail_commits == 1);
        sd_mutex_fail_give = true; assert(!commit_power_down_test());
        assert(rail_commits == 1); check_poison(); goto done;
    }
#endif
    assert(!"unknown scenario");
done:
    printf(TRANSPORT_NAME " storage admission: %s PASS\n", scenario); return 0;
}
