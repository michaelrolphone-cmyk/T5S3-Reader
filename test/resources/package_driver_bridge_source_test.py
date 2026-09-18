#!/usr/bin/env python3
"""Guard live driver intake, downloader exclusivity, recovery and unified UI wiring."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / 'src/runtime/drivers/DriverPackage.cpp').read_text(encoding='utf-8')
start = source.index('bool installStagedDriverPackage(')
end = source.index('\nbool validateGpsDriverPackage()', start)
installer = source[start:end]
for required in ('publishOrdinaryPackage(', 'ordinaryTransactionPaths(',
                 'recoverDriverDirectory(info.id)', 'Storage.exists(paths.stage)',
                 'Storage.mkdir(paths.stage, false)', 'verify(paths.stage, observed)',
                 'Installed driver', 'activation unchanged'):
    assert required in installer, required
assert 'publishDirectoryTransaction(' not in installer
assert 'removeManagedDirectory(stage)' not in installer

bridge = (root / 'src/native/NativeDriverManagerBridge.cpp').read_text(encoding='utf-8')
start = bridge.index('bool install(uint32_t index)')
end = bridge.index('\nbool rebuildRecoveryInventory()', start)
intake = bridge[start:end]
for required in ('ManagerMutation mutation;', 'if (!mutation)',
                 'const CatalogDriver selected = catalog[index];',
                 'getInstalledDriverVersion(selected.info.id',
                 'RuntimeDrivers::decideDriverDownload(',
                 'Storage.exists(temporaryStoragePath)',
                 'Storage.exists(paths.stage)',
                 'Storage.exists(paths.backup)',
                 'Storage.exists(paths.removing)',
                 'RuntimePackages::systemPackageUseGate().pinned(paths.target)',
                 'HttpDownloader::downloadToFile(',
                 'installStagedDriverPackage(selected.manifest'):
    assert required in intake, required
assert intake.index('getInstalledDriverVersion(') < intake.index('HttpDownloader::downloadToFile(')
assert intake.index('decideDriverDownload(') < intake.index('HttpDownloader::downloadToFile(')
assert intake.index('Storage.exists(temporaryStoragePath)') < intake.index('HttpDownloader::downloadToFile(')
assert intake.index('systemPackageUseGate().pinned(paths.target)') < intake.index('connectSavedWifi()', intake.index('decideDriverDownload('))
assert intake.index('systemPackageUseGate().pinned(paths.target)') < intake.index('HttpDownloader::downloadToFile(')
assert 'Storage.remove(temporaryStoragePath);\n    const auto result' not in intake
assert bridge.index('ManagerMutation mutation;', bridge.index('bool catalogRefresh()')) < bridge.index('catalog.clear();', bridge.index('bool catalogRefresh()'))

recovery = bridge[bridge.index('bool rebuildRecoveryInventory()'):]
for name in ('bool recoveryRefresh()', 'bool recoveryGet(',
             'bool recoveryRetry(', 'bool recoveryDiscard('):
    assert name in recovery, name
for name in ('bool recoveryRefresh()', 'bool recoveryRetry(', 'bool recoveryDiscard('):
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
assert 'recoveryRefresh,\n    recoveryCount,\n    recoveryGet,\n    recoveryRetry,\n    recoveryDiscard,' in recovery

api = (root / 'lib/NativeApps/include/T5DriverManagerApi.h').read_text(encoding='utf-8')
assert api.index('bool (*install)') < api.index('bool (*recovery_refresh)') < api.index('bool (*recovery_discard)')
ui = (root / 'Apps/driver_manager.c').read_text(encoding='utf-8')
start = ui.index('__attribute__((visibility("default"))) void app_main(void)')
app = ui[start:]
assert app.index('recovery_screen(drivers, ui)') < app.index('load_release(drivers, ui)')
assert 'static bool confirm(' in ui and 'return menu(ui, "Confirm operation", id, options, 2) == 1;' in ui
assert 'api->recovery_retry((uint32_t)selected)' in ui
assert 'api->recovery_discard((uint32_t)selected)' in ui
assert 'manager->preview(entry.name, &info)' in ui
assert 'info.kind != T5_PACKAGE_DRIVER' in ui
assert 'manager->install(folders[selected])' in ui
assert 'manager->uninstall(T5_PACKAGE_DRIVER, info.id)' in ui
assert 'release_indices[selected]' in ui  # sparse catalog rows are not release indices

# Staged downloads never overwrite or delete someone else's .part file.
download = (root / 'src/network/HttpDownloader.cpp').read_text(encoding='utf-8')
start = download.index('HttpDownloader::DownloadError HttpDownloader::downloadToFile(')
transfer = download[start:]
for required in ('if (staged && (destPath.front()', 'Storage.exists(destPath.c_str())',
                 'bool destinationCreated = false;', '&transferred, &destinationCreated);',
                 'if (destinationCreated) Storage.remove(destPath.c_str());',
                 'if (staged) {\n    file = Storage.open(destPath.c_str(), O_WRONLY | O_CREAT | O_EXCL);',
                 'Storage.openFileForWrite("HTTP", destPath.c_str(), file);'):
    assert required in transfer, required
assert transfer.index('Storage.exists(destPath.c_str())') < transfer.index('const auto* streams = invocationStreams(')
assert 'if (Storage.exists(destPath.c_str())) Storage.remove(destPath.c_str());' in transfer.split('} else {', 1)[-1]
stream = (root / 'src/runtime/streams/HttpStreamTransfer.h').read_text(encoding='utf-8')
stream_download = stream[stream.index('inline Result download('):]
assert 'if (destinationCreated) *destinationCreated = false;' in stream_download
assert 'T5_STREAM_FILE_CREATE_NEW' in stream_download
assert stream_download.index('T5_STREAM_FILE_CREATE_NEW') < stream_download.index('if (destinationCreated) *destinationCreated = true;')
print('Driver download and recovery: exclusive stage, serialized mutation, unified offline UI, confirmed retry/discard PASS')
