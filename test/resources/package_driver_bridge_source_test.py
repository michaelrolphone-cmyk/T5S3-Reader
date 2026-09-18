#!/usr/bin/env python3
"""Guard live driver intake, downloader exclusivity and publication.

Source assertions prove wiring, not physical SD or USB behavior. The executable
C++ intake tests validate the decision policy and board CI compiles adapters.
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
end = bridge.index('\nconst t5_driver_manager_api_v1 api', start)
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

# Neither transport may truncate an existing .part. A failed native CREATE_NEW
# must also leave another writer's file alone: cleanup requires proof of ownership.
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
print('Driver intake refuses mapped drivers; exclusive staged creation and owner-only cleanup are wired in both HTTP routes')
