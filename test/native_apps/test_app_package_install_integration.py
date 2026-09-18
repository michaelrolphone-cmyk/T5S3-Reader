#!/usr/bin/env python3
"""Prevent regressions from the live App Store back to split ELF/JSON swaps.

Transaction fault-injection tests cover rename/delete states; firmware board
builds compile the production HalStorage/mbedTLS adapters. This contract checks
live wiring and the fail-closed interrupted-download retry path.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOST = (ROOT / 'src/native/NativeAppHost.cpp').read_text(encoding='utf-8')
ADAPTER = (ROOT / 'src/native/AppPackageInstaller.cpp').read_text(encoding='utf-8')
INVENTORY = (ROOT / 'src/native/AppPackageRecoveryInventory.cpp').read_text(encoding='utf-8')
ONLINE = (ROOT / 'src/native/NativeOnlineAppInstall.h').read_text(encoding='utf-8')
RECOVERY = (ROOT / 'src/native/NativeOnlinePackageRecovery.h').read_text(encoding='utf-8')
SD_ADAPTER = (ROOT / 'src/runtime/packages/PackageOrdinarySdAdapter.cpp').read_text(encoding='utf-8')
MANAGED = (ROOT / 'src/runtime/packages/PackageOrdinaryManagedInstall.h').read_text(encoding='utf-8')
PACKAGE_MANAGER = (ROOT / 'src/native/NativePackageManagerBridge.cpp').read_text(encoding='utf-8')


class LiveInstallContract(unittest.TestCase):
    def test_live_installer_is_digest_verified_and_publishes_pair(self):
        start = HOST.index('bool appCatalogDownload(uint32_t index)')
        end = HOST.index('\nbool installedRefresh()', start)
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
        for obsolete in ('RuntimePackages::clearAppStage(',
                         'RuntimePackages::publishAppPair(',
                         'Storage.remove(destination.c_str())'):
            self.assertNotIn(obsolete, install)

        # New paths may reclaim only an exactly matched previous online source.
        # Retain exclusive new-directory/file creation and verified publication.
        for required in ('safePackageEntryName(artifact)',
                         'Recovery::discardMatchingInbox(',
                         'if (!Storage.mkdir(root.c_str(), false)) return false',
                         'O_WRONLY | O_CREAT | O_EXCL',
                         'HttpDownloader::downloadToFile(url, elfStage,',
                         '!Storage.rename(elfStage.c_str(), elfPath.c_str())',
                         '!verifyAppPair(elfPath.c_str(), jsonPath.c_str(), artifact, true)',
                         'mbedtls_sha256_ret(',
                         'parseOrdinaryManifest(descriptor.get(), static_cast<size_t>(count), *plan)',
                         'Recovery::discardMatchingStage(',
                         'installOrdinaryFromSd(root.c_str(), policy,',
                         'installed.result != OrdinaryInstallResult::Installed'):
            self.assertIn(required, ONLINE)
        self.assertLess(ONLINE.index('parseOrdinaryManifest(descriptor.get(),'),
                        ONLINE.index('Storage.mkdir(root.c_str(), false)'))
        self.assertLess(ONLINE.index('downloadToFile(url, elfStage,'),
                        ONLINE.index('verifyAppPair(elfPath.c_str()'))
        self.assertLess(ONLINE.index('verifyAppPair(elfPath.c_str()'),
                        ONLINE.index('discardMatchingStage(root,'))
        self.assertLess(ONLINE.index('discardMatchingStage(root,'),
                        ONLINE.index('installOrdinaryFromSd(root.c_str()'))
        self.assertIn('new (std::nothrow) char[4096]', ONLINE)
        self.assertIn('new (std::nothrow) OrdinaryPackagePlan', ONLINE)

    def test_recovery_is_exact_inventory_and_preserves_foreign_files(self):
        for required in ('equalFile(root + "/" + sidecarName, sidecar.data()',
                         'descriptorSeen && !equalFile(',
                         'else { valid = false; break; }',
                         'if (!valid || !closed || !sidecarSeen',
                         'verifyOrdinarySdDirectory(sourceRoot.c_str(), policy, resolver, verified)',
                         'stagePrefixMatches(',
                         'if (!valid || !closed) return false;',
                         'Storage.rmdir(paths.stage)'):
            self.assertIn(required, RECOVERY)
        self.assertLess(RECOVERY.index('verifyOrdinarySdDirectory(sourceRoot.c_str()'),
                        RECOVERY.index('Storage.rmdir(paths.stage)'))

    def test_package_manager_and_shared_verifier_do_not_retain_large_stack_buffers(self):
        self.assertIn('new (std::nothrow) char[4096]', PACKAGE_MANAGER)
        self.assertIn('new (std::nothrow) RuntimePackages::OrdinaryPackagePlan', PACKAGE_MANAGER)
        self.assertIn('new (std::nothrow) char[PackageJsonGuard::kMaxBytes]', MANAGED)
        self.assertIn('new (std::nothrow) OrdinaryPackagePlan', MANAGED)
        self.assertIn('new (std::nothrow) SdStage()', SD_ADAPTER)
        self.assertIn('new (std::nothrow) char[kManifestBytes]', SD_ADAPTER)
        self.assertNotIn('char metadata[kManifestBytes]{};', SD_ADAPTER)

    def test_real_adapter_checks_content_and_mapped_executable(self):
        for required in ('mbedtls_sha256_starts_ret', 'mbedtls_sha256_update_ret',
                         'mbedtls_sha256_finish_ret', 'declaredSha[2 * i]',
                         'declaredSize.as<unsigned>()', 'native_app_current_path()',
                         'recoverPairTransaction(', 'publishPairTransaction(',
                         '!safePackageEntryName(filename)',
                         'verifyNamedPair(paths.stageElf.c_str(), paths.stageManifest.c_str(), filename, true)'):
            self.assertIn(required, ADAPTER)
        self.assertLess(ADAPTER.index('native_app_current_path()'),
                        ADAPTER.index('publishPairTransaction('))

    def test_launch_and_version_query_validate_installed_content(self):
        self.assertIn('RuntimePackages::verifyAppPair(destination.c_str(), sidecar.c_str(), fileName, false)', HOST)
        self.assertIn('RuntimePackages::verifyAppPair(elf.c_str(), sidecar.c_str(), filename.c_str(), false)', HOST)
        self.assertIn('Application ELF integrity validation failed.', HOST)
        self.assertIn('Application update cannot be safely recovered.', HOST)

    def test_recovery_runs_before_springboard_presence_and_inventory_open(self):
        start = HOST.index('bool runNativeSpringboard(')
        boot = HOST[start:]
        self.assertLess(boot.index('recoverAppInventory()'),
                        boot.index('Storage.exists("/Apps/springboard.elf")'))
        self.assertLess(boot.index('recoverAppPair("springboard.elf")'),
                        boot.index('Storage.exists("/Apps/springboard.elf")'))
        installed = HOST[HOST.index('bool installedRefresh()'):HOST.index('\nuint32_t installedCount()')]
        self.assertLess(installed.index('recoverAppInventory()'),
                        installed.index('Storage.open("/Apps", O_RDONLY)'))
        self.assertIn('recoverAppPair(selectedName.c_str())', boot)

    def test_inventory_does_not_rename_open_directory_or_mapped_app(self):
        self.assertIn('appRecoveryCandidate(name, elf)', INVENTORY)
        self.assertLess(INVENTORY.index('directory.close();\n  if (!complete)'),
                        INVENTORY.index('recoverAppPair(elf.c_str())'))
        self.assertIn('native_app_current_path()', INVENTORY)
        self.assertIn('if (active && mapped == active && backedUp)', INVENTORY)
        self.assertIn('if (!Storage.exists(manifest.c_str()) && !backedUp && !staged) continue;', INVENTORY)
        self.assertIn('candidates.size() >= 256', INVENTORY)


if __name__ == '__main__':
    unittest.main()
