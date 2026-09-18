#!/usr/bin/env python3
"""Guard live driver intake and publication in addition to host transactions.

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
                 'HttpDownloader::downloadToFile(',
                 'installStagedDriverPackage(selected.manifest'):
    assert required in intake, required
assert intake.index('getInstalledDriverVersion(') < intake.index('HttpDownloader::downloadToFile(')
assert intake.index('decideDriverDownload(') < intake.index('HttpDownloader::downloadToFile(')
assert intake.index('Storage.exists(temporaryStoragePath)') < intake.index('HttpDownloader::downloadToFile(')
assert 'Storage.remove(temporaryStoragePath);\n    const auto result' not in intake
assert bridge.index('ManagerMutation mutation;', bridge.index('bool catalogRefresh()')) < bridge.index('catalog.clear();', bridge.index('bool catalogRefresh()'))
print('Driver Manager intake preserves prior downloads, rejects invalid versions before network mutation, and publishes through typed transaction')
