#!/usr/bin/env python3
"""Guard ordinary driver ZIP intake, scoped legacy recovery, and exclusive download."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
package = (root / 'src/runtime/drivers/DriverPackage.cpp').read_text(encoding='utf-8')
start = package.index('bool installStagedDriverPackage(')
end = package.index('\nbool validateGpsDriverPackage()', start)
installer = package[start:end]
for required in ('publishOrdinaryPackage(', 'ordinaryTransactionPaths(',
                 'recoverDriverDirectory(info.id)', 'Storage.exists(paths.stage)',
                 'Storage.mkdir(paths.stage, false)', 'verify(paths.stage, observed)',
                 'activation unchanged'):
    assert required in installer, required
assert 'publishDirectoryTransaction(' not in installer
assert 'removeManagedDirectory(stage)' not in installer

# The new archive is downloaded from the exact catalog-pinned release, SHA
# checked, then passed through the same ordinary ZIP transaction as SD intake.
online = (root / 'src/native/NativeOnlineRtePackageInstall.h').read_text(encoding='utf-8')
for required in ('releases/download/', 'archiveMatches(',
                 'HttpDownloader::downloadToFile(url, part, progress)',
                 'Storage.rename(part.c_str(), archive.c_str())',
                 'installOrdinaryFromSdZip(', '&package.identity',
                 'OrdinaryInstallResult::Installed'):
    assert required in online, required
assert 'releases/latest/download/' not in online
assert online.index('archiveMatches(part.c_str(), package)') < online.index('Storage.rename(part.c_str(), archive.c_str())')
assert 'Storage.exists(part.c_str())' in online
bridge = (root / 'src/native/NativePackageManagerBridge.cpp').read_text(encoding='utf-8')
assert 'RuntimeOnlinePackages::Catalog::selected(' in bridge
assert 'RuntimeOnlinePackages::OrdinaryZip::install(' in bridge
assert 'candidate, release, callback' in bridge
assert 'Mutation lock;' in bridge
assert 'installOrdinaryFromSdZip(' in bridge
assert 'systemPackageUseGate().pinned(paths.target)' in bridge

# Historical driver-stage repair and normal package transaction mutate the
# same /Drivers stage/backup/target directories. Guard the entire operation
# and its recovery inventory with ONE cross-translation-unit atomic flag.
gate = (root / 'src/runtime/packages/PackageMutationGate.h').read_text(encoding='utf-8')
assert 'inline std::atomic_flag& packageMutationFlag()' in gate
assert 'static std::atomic_flag gate = ATOMIC_FLAG_INIT;' in gate
assert 'test_and_set(std::memory_order_acquire)' in gate
assert 'clear(std::memory_order_release)' in gate
assert 'ScopedPackageMutation(const ScopedPackageMutation&) = delete;' in gate
assert 'using Mutation = RuntimePackages::ScopedPackageMutation;' in bridge
assert 'std::atomic_flag mutation =' not in bridge
for operation in ('bool install(', 'bool installArchive(', 'bool uninstall(',
                  'bool onlineRefresh('):
    block = bridge[bridge.index(operation):].split('\n}', 1)[0]
    assert 'Mutation lock;' in block, operation
online_common = bridge[bridge.index('bool onlineInstallCommon('):].split('\n}', 1)[0]
assert 'Mutation lock;' in online_common
assert 'return onlineInstallCommon(index, nullptr, nullptr);' in bridge

# All normal operations in the Driver Manager are common package operations.
# No transport-specific URL, device-class identity or loose ELF install may
# remain callable via the historical driver manager's ABI.
ui = (root / 'Apps/driver_manager.c').read_text(encoding='utf-8')
app = ui[ui.index('__attribute__((visibility("default"))) void app_main(void)'):]
assert app.index('recovery_screen(driver, ui)') < app.index('refresh_online(manager, ui)')
assert 'manager->online_refresh()' in ui
assert 'manager->online_get(i, &item)' in ui
assert 'manager->online_install(online_index[selected])' in ui
assert 'manager->preview_archive(entry.name, &p)' in ui
assert 'manager->install_archive(offline_name[selected])' in ui
assert 'manager->uninstall(T5_PACKAGE_DRIVER, p->id)' in ui
assert 'driver->recovery_retry((uint32_t)selected)' in ui
assert 'driver->recovery_discard((uint32_t)selected)' in ui
assert 'confirm(ui, item.id, "Discard retained files")' in ui
assert 'catalog_refresh(' not in ui and 'install_with_progress(' not in ui
assert 'else populate_online(manager);' in app
assert 'else if (!refresh_online(manager, ui))' not in app

legacy = (root / 'src/native/NativeDriverManagerBridge.cpp').read_text(encoding='utf-8')
assert 'NativeOnlineDriverInstall.h' not in legacy
assert 'HttpDownloader' not in legacy and 'latest/download/' not in legacy
assert 'usb-provider-catalog.json' not in legacy and 'driver-catalog.json' not in legacy
assert 'bool catalogRefresh() { return false; }' in legacy
assert 'bool install(uint32_t) { return false; }' in legacy
assert 'bool managerApp()' in legacy
assert 'native_app_current_path()' in legacy
assert '"/sd/Apps/driver_manager/driver_manager.elf"' in legacy
assert 'version == T5_DRIVER_MANAGER_API_VERSION && managerApp()' in legacy
assert 'using ManagerMutation = RuntimePackages::ScopedPackageMutation;' in legacy
assert 'std::atomic_flag managerMutation =' not in legacy
recovery = legacy[legacy.index('bool rebuildRecoveryInventory()'):]
for name in ('bool recoveryRefresh()', 'uint32_t recoveryCount()', 'bool recoveryGet(',
             'bool recoveryRetry(', 'bool recoveryDiscard('):
    function = recovery[recovery.index(name):]
    assert 'ManagerMutation mutation;' in function.split('\n}', 1)[0], name
for required in ('RuntimePackages::safeId(id.c_str())',
                 'RuntimeDrivers::inspectDriverStage(',
                 'RuntimeDrivers::retryDriverStage(',
                 'RuntimeDrivers::discardDriverStage(',
                 'const bool regular = !part.isDirectory();',
                 'okay = regular && closed && Storage.remove(kDownloadStage);'):
    assert required in recovery, required
assert 'Storage.remove(paths.target)' not in recovery

# Unknown .part files belong to their creator, not to a failed new download.
download = (root / 'src/network/HttpDownloader.cpp').read_text(encoding='utf-8')
transfer = download[download.index('HttpDownloader::DownloadError HttpDownloader::downloadToFile('):]
for required in ('Storage.exists(destPath.c_str())',
                 'bool destinationCreated = false;', '&transferred, &destinationCreated, &httpOpenStatus);',
                 'if (destinationCreated) Storage.remove(destPath.c_str());',
                 'Storage.open(destPath.c_str(), O_WRONLY | O_CREAT | O_EXCL);'):
    assert required in transfer, required
assert transfer.index('Storage.exists(destPath.c_str())') < transfer.index('const auto* streams = invocationStreams(')
stream = (root / 'src/runtime/streams/HttpStreamTransfer.h').read_text(encoding='utf-8')
stream_download = stream[stream.index('inline Result download('):]
assert 'T5_STREAM_FILE_CREATE_NEW' in stream_download
assert stream_download.index('T5_STREAM_FILE_CREATE_NEW') < stream_download.index('if (destinationCreated) *destinationCreated = true;')
print('Driver ZIP intake, shared transaction gate, scoped recovery, legacy ABI retirement and exclusive staging guards passed')
