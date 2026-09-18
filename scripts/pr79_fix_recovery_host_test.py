#!/usr/bin/env python3
"""Align regression tests with actual nested UI and canonical online install contracts."""
from pathlib import Path

path = Path('test/native_apps/driver_manager_test.c')
source = path.read_text(encoding='utf-8')
old = '''    retained_state = T5_DRIVER_RECOVERY_MAPPED;
    retained_retry = false; retained_discard = false; retained_count = 1;
    reset_queue(); enqueue(T5_UI_EVENT_CONFIRM); enqueue(T5_UI_EVENT_BACK);
    assert(recovery_screen(&manager, &recovery_ui));
'''
new = '''    retained_state = T5_DRIVER_RECOVERY_MAPPED;
    retained_retry = false; retained_discard = false; retained_count = 1;
    reset_queue();
    enqueue(T5_UI_EVENT_CONFIRM); // Open actions: only Cancel is available.
    enqueue(T5_UI_EVENT_BACK);    // Cancel the nested action menu.
    enqueue(T5_UI_EVENT_BACK);    // Then leave the recovery screen.
    assert(recovery_screen(&manager, &recovery_ui));
'''
if old in source:
    path.write_text(source.replace(old, new, 1), encoding='utf-8')
elif new in source:
    print('Recovery host test already updated')
else:
    raise SystemExit('Unexpected recovery test layout; refusing to patch')

contract_path = Path('test/native_apps/test_app_package_install_integration.py')
contract = contract_path.read_text(encoding='utf-8')
old_decl = "INVENTORY = (ROOT / 'src/native/AppPackageRecoveryInventory.cpp').read_text(encoding='utf-8')\n"
new_decl = old_decl + "ONLINE = (ROOT / 'src/native/NativeOnlineAppInstall.h').read_text(encoding='utf-8')\n"
if new_decl not in contract:
    if contract.count(old_decl) != 1:
        raise SystemExit('Cannot find App Store contract declarations')
    contract = contract.replace(old_decl, new_decl, 1)

start = contract.index('    def test_live_installer_is_digest_verified_and_publishes_pair(self):')
end = contract.index('    def test_real_adapter_checks_content_and_mapped_executable(self):', start)
replacement = '''    def test_live_installer_is_digest_verified_and_publishes_pair(self):
        start = HOST.index('bool appCatalogDownload(uint32_t index)')
        end = HOST.index('\\nbool installedRefresh()', start)
        install = HOST[start:end]
        # Source metadata is checked before the canonical online adapter is invoked.
        for required in ('RuntimePackages::safePackageEntryName',
                         'metadata["size_bytes"].as<unsigned>() != selected.size',
                         'metadata["sha256"].is<const char*>()',
                         'RuntimePackages::validSha256Hex(digest)',
                         'RuntimePackages::comparePackageVersions(',
                         'RuntimeOnlinePackages::installApplication(selected.name.c_str()'):
            self.assertIn(required, install)
        self.assertLess(install.index('metadata["sha256"]'),
                        install.index('RuntimeOnlinePackages::installApplication('))
        # The new online path cannot silently fall back to legacy split-file swaps.
        for obsolete in ('RuntimePackages::clearAppStage(',
                         'RuntimePackages::publishAppPair(',
                         'Storage.remove(destination.c_str())'):
            self.assertNotIn(obsolete, install)

        # Verify the real online adapter's exclusive stream stage, pair digest,
        # canonical manifest preflight and unified transactional publication.
        for required in ('safePackageEntryName(artifact)',
                         'Storage.exists(root.c_str()) || !Storage.mkdir(root.c_str(), false)',
                         'O_WRONLY | O_CREAT | O_EXCL',
                         'HttpDownloader::downloadToFile(url, elfStage,',
                         '!Storage.rename(elfStage.c_str(), elfPath.c_str())',
                         '!verifyAppPair(elfPath.c_str(), jsonPath.c_str(), artifact, true)',
                         'mbedtls_sha256_ret(',
                         'parseOrdinaryManifest(descriptor, static_cast<size_t>(count), plan)',
                         'installOrdinaryFromSd(root.c_str(), policy,',
                         'installed.result != OrdinaryInstallResult::Installed'):
            self.assertIn(required, ONLINE)
        self.assertLess(ONLINE.index('downloadToFile(url, elfStage,'),
                        ONLINE.index('verifyAppPair(elfPath.c_str()'))
        self.assertLess(ONLINE.index('verifyAppPair(elfPath.c_str()'),
                        ONLINE.index('parseOrdinaryManifest(descriptor,'))
        self.assertLess(ONLINE.index('parseOrdinaryManifest(descriptor,'),
                        ONLINE.index('installOrdinaryFromSd(root.c_str()'))

'''
if 'RuntimePackages::publishAppPair(' in contract[start:end]:
    contract = contract[:start] + replacement + contract[end:]
elif contract[start:end] != replacement:
    raise SystemExit('Unexpected App Store live transaction test; refusing to replace')
contract_path.write_text(contract, encoding='utf-8')
