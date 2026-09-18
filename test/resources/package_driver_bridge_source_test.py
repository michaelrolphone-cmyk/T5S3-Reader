#!/usr/bin/env python3
"""Guard live driver intake, downloader exclusivity, and recovery wiring.

Source assertions verify production linkage; host recovery/UI tests exercise
behavior. Board CI compiles all firmware and released native ELF variants.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / 'src/runtime/drivers/DriverPackage.cpp').read_text(encoding='utf-8')
start = source.index('bool installStagedDriverPackage(')
end = source.index('\nbool validateGpsDriverPackage()', start)
installer = source[start:end]
assert 'publishOrdinaryPackage(' in installer
assert 'ordinaryTransactionPaths(' in installer
assert 'recoverDriverDirectory(info.id)' in installer
assert 'Storage.exists(paths.stage)' in installer
assert 'Storage.mkdir(paths.stage, false)' in installer
assert 'publishDirectoryTransaction(' not in installer
assert 'removeManagedDirectory(stage)' not in installer
assert 'verify(paths.stage, observed)' in installer
assert 'Installed driver' in installer and 'activation unchanged' in installer

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

# The SAME guard serializes recovery with normal installs and refresh. Discover
# stages independently of GitHub catalog, and delete only the reserved .part
# after checking it is a regular file. Stage discard uses the package lease.
recovery = bridge[bridge.index('bool rebuildRecoveryInventory()'):]
for name in ('bool recoveryRefresh()', 'bool recoveryGet(',
             'bool recoveryRetry(', 'bool recoveryDiscard('):
    assert name in recovery, name
for name in ('bool recoveryRefresh()', 'bool recoveryRetry(', 'bool recoveryDiscard('):
    function = recovery[recovery.index(name):]
    assert 'ManagerMutation mutation;' in function.split('\n}', 1)[0], name
assert 'RuntimePackages::safeId(id.c_str())' in recovery
assert 'RuntimeDrivers::inspectDriverStage(' in recovery
assert 'RuntimeDrivers::retryDriverStage(' in recovery
assert 'RuntimeDrivers::discardDriverStage(' in recovery
assert 'const bool regular = !part.isDirectory();' in recovery
assert 'okay = regular && closed && Storage.remove(kDownloadStage);' in recovery
assert 'Storage.remove(paths.target)' not in recovery
assert 'recoveryRefresh,\n    recoveryCount,\n    recoveryGet,\n    recoveryRetry,\n    recoveryDiscard,' in recovery

api = (root / 'lib/NativeApps/include/T5DriverManagerApi.h').read_text(encoding='utf-8')
assert api.index('bool (*install)') < api.index('bool (*recovery_refresh)') < api.index('bool (*recovery_discard)')
ui = (root / 'Apps/driver_manager.c').read_text(encoding='utf-8')
start = ui.index('__attribute__((visibility("default"))) void app_main(void)')
assert ui.index('show_recovery_screen(drivers, ui)', start) < ui.index('refresh_catalog(drivers, ui)', start)
assert 'static bool confirm_discard(' in ui
assert 'return selected == 1;' in ui
assert 'recovery_action_menu(api, ui,' in ui
assert 'api->recovery_retry(index)' in ui and 'api->recovery_discard(index)' in ui

# Neither transport may truncate an existing .part, nor delete a stage whose
# exclusive creation failed. Non-staged downloads retain overwrite behavior.
download = (root / 'src/network/HttpDownloader.cpp').read_text(encoding='utf-8')
start = download.index('HttpDownloader::DownloadError HttpDownloader::downloadToFile(')
transfer = download[start:]
assert 'if (staged && (destPath.front()' in transfer
assert 'Storage.exists(destPath.c_str())' in transfer
assert transfer.index('Storage.exists(destPath.c_str())') < transfer.index('const auto* streams = invocationStreams(')
assert 'bool destinationCreated = false;' in transfer
assert '&transferred, &destinationCreated);' in transfer
assert 'if (destinationCreated) Storage.remove(destPath.c_str());' in transfer
assert 'if (staged) {\n    file = Storage.open(destPath.c_str(), O_WRONLY | O_CREAT | O_EXCL);' in transfer
assert 'if (Storage.exists(destPath.c_str())) Storage.remove(destPath.c_str());' in transfer.split('} else {', 1)[-1]
assert 'Storage.openFileForWrite("HTTP", destPath.c_str(), file);' in transfer

stream = (root / 'src/runtime/streams/HttpStreamTransfer.h').read_text(encoding='utf-8')
stream_download = stream[stream.index('inline Result download('):]
assert 'if (destinationCreated) *destinationCreated = false;' in stream_download
assert 'T5_STREAM_FILE_CREATE_NEW' in stream_download
assert stream_download.index('T5_STREAM_FILE_CREATE_NEW') < stream_download.index('if (destinationCreated) *destinationCreated = true;')
print('Driver download and recovery: exclusive staged creation, shared serialization, offline UI, confirmed discard and retry wiring PASS')
