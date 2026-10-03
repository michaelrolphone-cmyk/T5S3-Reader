// Actual provider/FatFs/HAL regression. Wrappers only observe calls or inject
// one failure/mutation after the real directory read has released its mutex.
static const risc_storage_volume_api_v1* dirTestApi;
static const risc_storage_volume_api_v1_ext* dirTestExt;
static unsigned dirTestStats;
static bool dirTestFailFileClose, dirTestFailDirClose;
static void (*dirTestAfterNext)(const risc_storage_dirent_v1&);
static bool dirTestStat(void* context, const char* path, uint64_t* size, bool* directory) {
    ++dirTestStats; return dirTestApi->stat(context, path, size, directory);
}
static bool dirTestNext(void* context, uint32_t handle, risc_storage_dirent_v1* entry) {
    if (!dirTestApi->dir_next(context, handle, entry)) return false;
    if (dirTestAfterNext) { auto fn=dirTestAfterNext; dirTestAfterNext=nullptr; fn(*entry); }
    return true;
}
static bool dirTestFileClose(void* context, uint32_t handle, bool commit) {
    if (dirTestFailFileClose) { dirTestFailFileClose=false; return false; }
    return dirTestApi->file_close(context, handle, commit);
}
static bool dirTestDirClose(void* context, uint32_t handle) {
    if (dirTestFailDirClose) { dirTestFailDirClose=false; return false; }
    return dirTestExt->dir_close_checked(context, handle);
}
static void dirTestWrite(const char* path, const char* bytes) {
    const auto handle=dirTestExt->file_open(nullptr,path,
        RISC_STORAGE_OPEN_WRITE|RISC_STORAGE_OPEN_CREATE|RISC_STORAGE_OPEN_TRUNCATE);
    assert(handle && dirTestApi->file_write(nullptr,handle,bytes,std::strlen(bytes))==std::strlen(bytes));
    assert(dirTestApi->file_close(nullptr,handle,true));
}
static void dirTestIoFailure(const risc_storage_dirent_v1&) {
    card_bad_crc=true;
    uint64_t size=0; bool directory=false;
    assert(!dirTestApi->stat(nullptr,"/force-root-sector-read",&size,&directory));
    assert(!dirTestApi->ready(nullptr));
}
static unsigned dirTestMutation;
static void dirTestMutate(const risc_storage_dirent_v1& entry) {
    assert(!std::strcmp(entry.name,"item"));
    assert(dirTestApi->remove(nullptr,"/race/item"));
    if (dirTestMutation==1) assert(dirTestExt->mkdir(nullptr,"/race/item"));
    if (dirTestMutation==2 || dirTestMutation==3) dirTestWrite("/race/item","replacement content");
}
static void directoryIterationTests(const char* scenario, const risc_driver_v2* driver,
                                    const risc_storage_volume_api_v1* api) {
    dirTestApi=api; dirTestExt=risc_storage_volume_extension(api);
    auto hooks=*dirTestExt; hooks.base.struct_size=sizeof(hooks);
    hooks.base.stat=dirTestStat; hooks.base.dir_next=dirTestNext;
    hooks.base.file_close=dirTestFileClose; hooks.dir_close_checked=dirTestDirClose;
    assert(Storage.bindVolume(&hooks.base));
    assert(Storage.mkdir("/race")); dirTestWrite("/race/item","original");
    Storage.invalidateObservations();
    if (!std::strcmp(scenario,"directory-races")) {
        // Missing child, file -> directory, directory -> file, and same-type
        // replacement. The open must check the real current object, not invent
        // a handle or use the size from the preceding metadata observation.
        for (unsigned mode=0;mode<5;++mode) {
            dirTestMutation=mode;
            if (mode==2 || mode==4) { assert(Storage.remove("/race/item")); assert(Storage.mkdir("/race/item")); }
            auto parent=Storage.open("/race"); assert(parent);
            dirTestAfterNext=dirTestMutate; dirTestStats=0;
            auto child=parent.openNextFile(); assert(dirTestStats==0);
            if (mode==3) {
                assert(child && !child.isDirectory() && child.fileSize64()==19);
                char bytes[20]{}; assert(child.read(bytes,19)==19 && !std::strcmp(bytes,"replacement content"));
                assert(child.close() && !parent.getError());
            } else assert(!child && parent.getError() && !parent.openNextFile());
            assert(parent.close()); Storage.invalidateObservations();
            if (mode==1) assert(Storage.rmdir("/race/item"));
            else if (mode==2 || mode==3) assert(Storage.remove("/race/item"));
            dirTestWrite("/race/item","original"); Storage.invalidateObservations();
            // A failed iterator does not poison a new bounded retry.
            auto retry=Storage.open("/race"); auto recovered=retry.openNextFile();
            assert(recovered && recovered.close()); assert(!retry.openNextFile() && !retry.getError());
            assert(retry.close());
        }
    } else if (!std::strcmp(scenario,"directory-io")) {
        auto parent=Storage.open("/race"); assert(parent);
        dirTestAfterNext=dirTestIoFailure;
        assert(!parent.openNextFile() && parent.getError());
        card_bad_crc=false; assert(parent.close() && Storage.begin());
        auto retry=Storage.open("/race"); auto child=retry.openNextFile();
        assert(child && child.close() && retry.close());
    } else if (!std::strcmp(scenario,"directory-unavailable")) {
        auto parent=Storage.open("/race"); assert(parent);
        Storage.markUnavailable(); assert(api->ready(nullptr) && !Storage.ready());
        assert(!parent.openNextFile() && parent.getError());
        assert(parent.close() && Storage.begin());
        auto retry=Storage.open("/race"); auto child=retry.openNextFile();
        assert(child && child.close() && retry.close());
    } else if (!std::strcmp(scenario,"directory-exhaustion")) {
        for (unsigned kind=0;kind<2;++kind) {
            if (kind) { assert(Storage.remove("/race/item")); assert(Storage.mkdir("/race/item")); }
            auto parent=Storage.open("/race"); assert(parent);
            uint32_t held[64]{}; unsigned count=0;
            while (count<64) {
                uint32_t h=kind ? api->dir_open(nullptr,"/race")
                    : dirTestExt->file_open(nullptr,"/race/item",RISC_STORAGE_OPEN_READ);
                if (!h) break;
                held[count++]=h;
            }
            assert(count && count<64 && !parent.openNextFile() && parent.getError());
            for(unsigned i=0;i<count;++i) assert(kind ? dirTestExt->dir_close_checked(nullptr,held[i])
                : api->file_close(nullptr,held[i],true));
            assert(parent.close());
            auto retry=Storage.open("/race"); auto child=retry.openNextFile();
            assert(child && child.isDirectory()==!!kind && child.close() && retry.close());
        }
    } else if (!std::strcmp(scenario,"directory-close-file") || !std::strcmp(scenario,"directory-close-dir")) {
        const bool directory=!std::strcmp(scenario,"directory-close-dir");
        if (directory) { assert(Storage.remove("/race/item")); assert(Storage.mkdir("/race/item")); }
        auto parent=Storage.open("/race"); auto child=parent.openNextFile(); assert(child);
        if (directory) dirTestFailDirClose=true; else dirTestFailFileClose=true;
        assert(!child.close() && child.isOpen() && child.getError());
        assert(!Storage.generation().quiescent && !Storage.begin() && !driver->quiesce());
        assert(child.close() && !child.isOpen() && parent.close());
        // Uncertain close cannot certify reusable observations even after retry.
        assert(!Storage.generation().quiescent && !Storage.begin());
    } else {
        assert(!std::strcmp(scenario,"directory-basic"));
        assert(Storage.mkdir("/race/a-long-directory-name"));
        assert(Storage.writeFile("/race/a-long-filename.txt",String("payload")));
        auto empty=Storage.open("/race/empty",O_WRONLY|O_CREAT); assert(empty && empty.close());
        assert(Storage.writeFile("/race/attrs.txt",String("attributes")));
        // The real on-card entry is read-only + hidden + system + archive. The
        // production configuration omits chmod, so edit the host card fixture
        // with no handles live and remount before testing actual FAT opens.
        unsigned attributes=0;
        for (size_t at=2080u*512u;at<(size_t)card_sectors*512u;at+=32u) {
            if (!std::memcmp(card_image+at,"ATTRS   TXT",11)) { card_image[at+11]=0x27; ++attributes; }
        }
        assert(attributes==1 && Storage.begin());
        assert(!Storage.open("/race/attrs.txt",O_WRONLY));
        auto parent=Storage.open("/race"); assert(parent);
        const auto before=Storage.generation(); assert(before.quiescent);
        dirTestStats=0; unsigned count=0;
        for (;;) {
            auto child=parent.openNextFile(); if (!child) break;
            char name[128]{}; assert(child.getName(name,sizeof(name)));
            if (child.isDirectory()) { assert(!std::strcmp(name,"a-long-directory-name")); assert(!child.openNextFile() && !child.getError()); }
            else if (!std::strcmp(name,"empty")) assert(child.fileSize64()==0);
            else { char bytes[16]{}; assert(child.read(bytes,sizeof(bytes))>0); }
            assert(child.close()); ++count;
        }
        assert(count==5 && !parent.getError() && Storage.unchanged(before));
        assert(dirTestStats==0); // Original HAL independently fails this assertion.
        // Remount cannot revoke live iterator/child ownership. Close then retry.
        assert(!Storage.begin()); parent.rewindDirectory(); auto child=parent.openNextFile();
        assert(child && parent.close() && !Storage.begin());
        assert(child.isOpen() && child.close() && Storage.begin());
        assert(!Storage.unchanged(before));
        auto retry=Storage.open("/race"); auto first=retry.openNextFile(); assert(first && first.close() && retry.close());
        // Failed path open preserves cleanup/remount; open-next I/O is tested separately.
        card_bad_crc=true; auto failed=Storage.open("/nonexistent-on-media"); assert(!failed);
        card_bad_crc=false; assert(Storage.begin());
        auto final=Storage.open("/race"); auto recovered=final.openNextFile(); assert(recovered && recovered.close() && final.close());
    }
    assert(driver->quiesce()); driver->stop(); assert(sd_mutex_creates==sd_mutex_deletes);
    std::free(card_image); std::printf("actual provider/HAL %s PASS\n",scenario);
}
