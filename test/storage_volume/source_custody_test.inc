// Execute source custody through the production HAL and real SD/FatFs provider.
// Wrappers observe callback admission; they do not implement storage behavior.
static const risc_storage_volume_api_v1* sourceTestApi;
static const risc_storage_volume_api_v1_power* sourceTestPower;
static const risc_storage_volume_api_v1_power_commit* sourceTestCommit;
static unsigned sourceRefreshes, sourcePrepares, sourceCancels, sourceCommits;
static bool sourceFailFileClose, sourceFailDirClose;
static uint32_t sourceLastClose;
static bool sourceFileClose(void* context, uint32_t handle, bool commit) {
    assert(FakeLock::held); sourceLastClose = handle;
    if (sourceFailFileClose) { sourceFailFileClose = false; return false; }
    return sourceTestApi->file_close(context, handle, commit);
}
static bool sourceDirClose(void* context, uint32_t handle) {
    assert(FakeLock::held); sourceLastClose = handle;
    if (sourceFailDirClose) { sourceFailDirClose = false; return false; }
    return risc_storage_volume_extension(sourceTestApi)->dir_close_checked(context, handle);
}
static bool sourceRefresh(void* context) {
    assert(FakeLock::held); ++sourceRefreshes;
    return sourceTestApi->refresh(context);
}
static bool sourcePrepare(void* context) {
    assert(FakeLock::held); ++sourcePrepares;
    return sourceTestPower->prepare_power_down(context);
}
static bool sourceCancel(void* context) {
    assert(FakeLock::held); ++sourceCancels;
    return sourceTestPower->cancel_power_down(context);
}
static bool sourceCommit(void* context) {
    assert(FakeLock::held); ++sourceCommits;
    return sourceTestCommit->commit_power_down(context);
}
static void sourceCustodyTests(const char* scenario, const risc_driver_v2* driver, const risc_storage_volume_api_v1* api) {
    sourceTestApi = api;
    sourceTestPower = risc_storage_volume_power(api);
    sourceTestCommit = risc_storage_volume_power_commit(api);
    risc_storage_volume_api_v1_power_commit hooks{};
    if (sourceTestCommit) hooks = *sourceTestCommit;
    else hooks.power = *sourceTestPower;
    auto* selected = &hooks.power.volume.base;
    selected->refresh = sourceRefresh;
    selected->file_close = sourceFileClose;
    hooks.power.volume.dir_close_checked = sourceDirClose;
    hooks.power.prepare_power_down = sourcePrepare;
    hooks.power.cancel_power_down = sourceCancel;
    if (sourceTestCommit) hooks.commit_power_down = sourceCommit;
    assert(Storage.bindVolume(selected));
    assert(Storage.writeFile("/source-probe", "selected source"));

    StorageSourceLease lease, second;
    if (std::strncmp(scenario, "source-close-", 13) == 0) {
        const bool directory = !std::strcmp(scenario, "source-close-dir");
        const bool lockFailure = std::strncmp(scenario, "source-close-lock", 17) == 0;
        const bool destructor = !std::strcmp(scenario, "source-close-destructor") ||
                                !std::strcmp(scenario, "source-close-lock-destructor");
        const bool move = !std::strcmp(scenario, "source-close-lock-move");
        const bool withoutLease = !std::strcmp(scenario, "source-close-no-lease") ||
                                  !std::strcmp(scenario, "source-close-lock-no-lease");
        if (!withoutLease) assert(Storage.captureNativeSource(lease));
        const auto saved = lease;
        if (destructor) {
            auto file = Storage.open("/source-probe"); assert(file);
            if (lockFailure) FakeLock::failNextTake = true;
            else sourceFailFileClose = true;
        } else {
            auto file = Storage.open(directory ? "/" : "/source-probe"); assert(file);
            if (lockFailure) {
                if (move) {
                    auto replacement = Storage.open("/source-probe"); assert(replacement);
                    FakeLock::failNextTake = true;
                    file = std::move(replacement);
                    assert(file.isOpen() && replacement.isOpen());
                    assert(replacement.close());
                } else {
                    FakeLock::failNextTake = true;
                    assert(!file.close() && file.isOpen());
                }
            } else {
                sourceFailDirClose = directory; sourceFailFileClose = !directory;
                assert(!file.close() && file.isOpen() && file.getError());
            }
            if (!withoutLease) {
                assert(!Storage.matchesNativeSource(lease) && !Storage.releaseNativeSource(lease));
                assert(!Storage.begin() && !Storage.prepareForSleep() && !Storage.commitSleep());
                assert(Storage.cancelSleep());
            }
            assert(!driver->quiesce()); // The provider still owns the failed handle.
            assert(file.close());
        }
        if (withoutLease) {
            assert(Storage.captureNativeSource(lease) && Storage.matchesNativeSource(lease));
            assert(Storage.releaseNativeSource(lease)); // No unrelated sticky source latch.
        } else {
            assert(!Storage.matchesNativeSource(lease) && !Storage.releaseNativeSource(lease));
            assert(lease.volume == saved.volume && lease.context == saved.context && lease.epoch == saved.epoch &&
                   lease.token == saved.token && lease.owner == saved.owner);
            assert(!Storage.captureNativeSource(second) && second.empty());
            assert(!Storage.bindVolume(api) && !Storage.begin() && !Storage.prepareForSleep() && !Storage.commitSleep());
            if (destructor) {
                assert(!driver->quiesce());
                // Even a subsequent proven provider close cannot clear a lost
                // Host close outcome or authorize release of retained custody.
                if (!lockFailure) {
                    assert(api->file_close(api->context, sourceLastClose, true));
                    assert(!Storage.releaseNativeSource(lease));
                }
            }
        }
        if (!(destructor && lockFailure)) {
            assert(driver->quiesce()); driver->stop();
        }
        std::free(card_image);
        std::printf("Production source close outcome retention: %s PASS\n", scenario);
        return;
    }
    const auto baseline = Storage.generation();
    assert(Storage.captureNativeSource(lease));
    assert(lease.volume == selected && lease.context == selected->context && lease.epoch && lease.token && !lease.owner);
    assert(Storage.unchanged(baseline) && Storage.matchesNativeSource(lease));
    assert(!Storage.captureNativeSource(second) && second.empty());
    assert(!Storage.captureNativeSource(lease) && Storage.matchesNativeSource(lease));
    const auto preflight = lease;
    assert(Storage.releaseNativeSource(lease) && lease.empty());
    assert(!Storage.matchesNativeSource(preflight) && !Storage.releaseNativeSource(lease));
    assert(Storage.captureNativeSource(lease));
    assert(lease.token > preflight.token && lease.epoch == preflight.epoch);

    auto rejectAltered = [&](StorageSourceLease altered) {
        const auto original = altered;
        const auto acquisitions = FakeLock::acquisitions;
        assert(!Storage.matchesNativeSource(altered));
        assert(!Storage.bindNativeSourceOwner(altered, 42));
        assert(!Storage.releaseNativeSource(altered));
        assert(FakeLock::acquisitions == acquisitions + 3);
        assert(altered.volume == original.volume && altered.context == original.context &&
               altered.epoch == original.epoch && altered.token == original.token && altered.owner == original.owner);
        assert(Storage.matchesNativeSource(lease));
    };
    auto altered = lease; altered.volume = api; rejectAltered(altered);
    int wrongContext = 0;
    altered = lease; altered.context = &wrongContext; rejectAltered(altered);
    altered = lease; ++altered.epoch; rejectAltered(altered);
    altered = lease; ++altered.token; rejectAltered(altered);
    altered = lease; ++altered.owner; rejectAltered(altered);
    rejectAltered({});
    assert(!Storage.bindNativeSourceOwner(lease, 0));
    const auto unbound = lease;
    assert(Storage.bindNativeSourceOwner(lease, 42) && lease.owner == 42);
    assert(!Storage.bindNativeSourceOwner(lease, 42) && !Storage.bindNativeSourceOwner(lease, 43));
    rejectAltered(unbound);

    const auto custodyStamp = Storage.generation();
    const auto reads = card_reads, writes = card_writes;
    assert(!Storage.bindVolume(api) && !Storage.bindVolume(selected) && !Storage.begin());
    assert(Storage.unchanged(custodyStamp) && !sourceRefreshes);
    assert(card_reads == reads && card_writes == writes && Storage.matchesNativeSource(lease));
    assert(Storage.reconcileExternalStorage() && Storage.unchanged(custodyStamp));
    assert(Storage.readFile("/source-probe") == "selected source");
    assert(Storage.unchanged(custodyStamp));

    // Direct selected handles are not HalStorage handles. Custody must fence
    // backend changes even before there is a handle and after the last closes.
    uint64_t bytes = 0;
    const auto direct = selected->file_open_read(selected->context, "/source-probe", &bytes);
    assert(direct && bytes == 15);
    assert(!Storage.prepareForSleep() && !Storage.commitSleep());
    assert(!sourcePrepares && !sourceCommits && Storage.ready());
    assert(Storage.cancelSleep() && sourceCancels == 1 && Storage.ready());
    char text[16]{};
    assert(selected->file_read(selected->context, direct, text, 15) == 15 && !std::strcmp(text, "selected source"));
    assert(sourceTestApi->file_close(selected->context, direct, true));
    assert(!Storage.prepareForSleep() && !Storage.begin());
    assert(Storage.unchanged(custodyStamp));

    // If another power participant already prepared, cancel must still restore
    // it after the HAL rejects prepare. Never fence this recovery with custody.
    assert(sourceTestPower->prepare_power_down(api->context) && !Storage.ready());
    assert(Storage.matchesNativeSource(lease));
    assert(!Storage.prepareForSleep() && !Storage.commitSleep());
    assert(Storage.cancelSleep() && sourceCancels == 2 && Storage.ready());
    assert(Storage.readFile("/source-probe") == "selected source");

    // Source identity does not certify unchanged contents or ready media.
    Storage.invalidateObservations();
    assert(!Storage.unchanged(custodyStamp) && Storage.matchesNativeSource(lease));
    Storage.externalStorageBegin(); Storage.externalStorageEnd(true);
    const auto unreconciled = Storage.generation();
    assert(!Storage.reconcileExternalStorage() && !sourceRefreshes);
    const auto refused = Storage.generation();
    assert(refused.mount == unreconciled.mount && refused.mutation == unreconciled.mutation);
    Storage.markUnavailable();
    assert(!Storage.ready() && Storage.matchesNativeSource(lease) && !Storage.begin());
    const auto released = lease;
    assert(Storage.releaseNativeSource(lease) && lease.empty()); // No live context lookup.
    assert(!Storage.matchesNativeSource(released));
    assert(Storage.reconcileExternalStorage() && sourceRefreshes == 1 && Storage.ready());
    assert(Storage.captureNativeSource(lease) && lease.epoch > released.epoch && lease.token > released.token);
    auto stale = released;
    assert(!Storage.releaseNativeSource(stale) && Storage.matchesNativeSource(lease));
    assert(Storage.releaseNativeSource(lease));

    // Cleanup enables the ordinary deferred sleep path with no retained handle.
    assert(Storage.prepareForSleep() && sourcePrepares == 1 && !Storage.ready());
    assert(!Storage.captureNativeSource(lease) && lease.empty());
    assert(Storage.cancelSleep() && sourceCancels == 3 && Storage.ready());
    assert(Storage.prepareForSleep() && sourcePrepares == 2);
    if (sourceTestCommit) {
        assert(Storage.commitSleep() && sourceCommits == 1 && card_sleep_off);
        assert(!Storage.cancelSleep());
    } else {
        assert(!Storage.commitSleep() && !sourceCommits);
        assert(Storage.cancelSleep() && Storage.ready());
        assert(driver->quiesce()); driver->stop();
    }
    assert(!card_bad_pin);
    std::free(card_image);
    std::puts("Production source custody: exact identity, exclusive token, owner binding, remount/sleep guards and recovery PASS");
}
