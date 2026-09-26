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
CATALOG_INDEX = (ROOT / 'src/native/AppCatalogIndex.cpp').read_text(encoding='utf-8')
APP_MANIFEST = (ROOT / 'src/native/AppManifest.cpp').read_text(encoding='utf-8')
MAIN = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')


class LiveInstallContract(unittest.TestCase):
    def test_live_installer_is_digest_verified_and_publishes_pair(self):
        start = HOST.index('bool appCatalogDownloadWithProgress(uint32_t index,')
        end = HOST.index('\nbool installedRefresh()', start)
        install = HOST[start:end]
        # Source metadata is checked before the canonical online adapter is invoked.
        for required in ('RuntimePackages::safePackageEntryName',
                         'metadata["size_bytes"].as<unsigned>() != selectedSize',
                         'metadata["sha256"].is<const char*>()',
                         'RuntimePackages::validSha256Hex(value)',
                         'RuntimePackages::comparePackageVersions(',
                         'const bool installedOk = RuntimeOnlinePackages::installApplication(',
                         'artifact.c_str(), version.c_str(), downloadUrl.c_str(), json,'):
            self.assertIn(required, install)
        self.assertLess(install.index('metadata["sha256"]'),
                        install.index('RuntimeOnlinePackages::installApplication('))
        self.assertIn('appCatalogDownloadWithProgress(uint32_t index,', install)
        self.assertIn('progress, progressContext, &failureReason', install)
        for obsolete in ('RuntimePackages::clearAppStage(',
                         'RuntimePackages::publishAppPair(',
                         'Storage.remove(destination.c_str())'):
            self.assertNotIn(obsolete, install)

        # Online installs discard only manager-owned stale scratch and always
        # rebuild from a fresh download; unknown content remains protected.
        for required in ('safePackageEntryName(artifact)',
                         'Recovery::discardOwnedInbox(',
                         'if (!Storage.mkdir(root.c_str(), false)) return fail("could not create package inbox")',
                         'O_WRONLY | O_CREAT | O_EXCL',
                         'const auto downloadResult = HttpDownloader::downloadToFile(',
                         'url, elfStage, [](size_t, size_t) { esp_task_wdt_reset(); }',
                         '!Storage.rename(elfStage.c_str(), elfPath.c_str())',
                         '!verifyAppPair(elfPath.c_str(), jsonPath.c_str(), artifact, true)',
                         'mbedtls_sha256_ret(',
                         'RuntimeMemory::PsramBuffer descriptor(4096)',
                         'RuntimeMemory::PsramBuffer planStorage(sizeof(OrdinaryPackagePlan))',
                         'parseOrdinaryManifest(descriptor.chars(), static_cast<size_t>(rebuiltCount), *plan)',
                         'Recovery::discardOwnedStage(',
                         'installOrdinaryFromSd(root.c_str(), policy,',
                         'installed.result != OrdinaryInstallResult::Installed'):
            self.assertIn(required, ONLINE)
        download_call = ONLINE.index(
            'const auto downloadResult = HttpDownloader::downloadToFile(')
        self.assertGreater(ONLINE.index('parseOrdinaryManifest(descriptor.chars(),'),
                           download_call)
        self.assertLess(download_call,
                        ONLINE.index('verifyAppPair(elfPath.c_str()'))
        self.assertLess(ONLINE.index('verifyAppPair(elfPath.c_str()'),
                        ONLINE.index('discardOwnedStage(*plan)'))
        self.assertLess(ONLINE.index('discardOwnedStage(*plan)'),
                        ONLINE.index('installOrdinaryFromSd(root.c_str()'))
        self.assertIn('RuntimeMemory::PsramBuffer descriptor(4096)', ONLINE)
        self.assertIn('RuntimeMemory::PsramBuffer planStorage(sizeof(OrdinaryPackagePlan))', ONLINE)
        self.assertIn('descriptor.reset();', ONLINE)
        self.assertLess(ONLINE.index('descriptor.reset();'), download_call)

        # The progress ABI must not trigger e-paper UI rendering while the HTTP
        # worker still owns TLS buffers. That transient 8 KiB refresh task can
        # consume the heap headroom required by the native stream transport.
        transfer_start = ONLINE.index('const auto downloadResult = HttpDownloader::downloadToFile(')
        transfer_end = ONLINE.index('if (progress) {', transfer_start)
        transfer = ONLINE[transfer_start:transfer_end]
        self.assertIn('[](size_t, size_t) { esp_task_wdt_reset(); }', transfer)
        self.assertIn('case HttpDownloader::HTTP_ERROR: return fail("HTTP download failed");', transfer)
        self.assertIn('case HttpDownloader::FILE_ERROR: return fail("download staging file failed");', transfer)
        self.assertIn('case HttpDownloader::STREAM_ERROR: return fail("download stream failed");', transfer)
        self.assertNotIn('progress(progressContext', transfer)
        terminal_progress = ONLINE.index('progress(progressContext, size, size);', transfer_end)
        self.assertGreater(terminal_progress, transfer_end)
        self.assertLess(ONLINE.index('delay(1);', transfer_end), terminal_progress)

    def test_failed_install_defers_catalog_refresh_until_installer_returns(self):
        start = HOST.index('bool appCatalogDownloadWithProgress(uint32_t index,')
        end = HOST.index('\nbool appCatalogDownload(uint32_t index)', start)
        install = HOST[start:end]
        self.assertIn('Selected catalog entry lost download metadata; reloading catalog', install)
        self.assertIn('const bool installedOk = RuntimeOnlinePackages::installApplication(', install)
        self.assertIn('s->catalogNeedsRefresh = true;', install)
        install_call = install.index('const bool installedOk = RuntimeOnlinePackages::installApplication(')
        after_install = install[install_call:]
        self.assertNotIn('loadAuthoritativeAppCatalog(s->catalog)', after_install)

        helper_start = HOST.index('bool ensureCatalogReady(Session* s)')
        helper_end = HOST.index('\nuint32_t appCatalogCount()', helper_start)
        helper = HOST[helper_start:helper_end]
        self.assertIn('if (!s->catalogNeedsRefresh) return true;', helper)
        self.assertIn('delay(1);', helper)
        self.assertIn('loadAuthoritativeAppCatalog(s->catalog)', helper)

    def test_online_recovery_discards_only_owned_scratch(self):
        for required in ('discardOwnedInbox(',
                         'discardOwnedStage(',
                         'else { valid = false; break; }',
                         'if (!valid || !closed) return false;',
                         'Storage.rmdir(root.c_str())',
                         'Storage.rmdir(paths.stage)'):
            self.assertIn(required, RECOVERY)
        # Exact-match helpers remain available for driver intake, but the
        # online application installer must no longer call resume/replay paths.
        self.assertIn('starting over', ONLINE)
        self.assertNotIn('discardMatchingInbox(', ONLINE)
        self.assertNotIn('discardMatchingStage(', ONLINE)
        self.assertNotIn('Recovered matching interrupted download', ONLINE)
        self.assertNotIn('interrupted package differs from this release', ONLINE)

    def test_app_catalog_metadata_is_psram_first_and_single_fetch(self):
        loader_start = HOST.index('bool loadIndependentAppIndex(')
        loader_end = HOST.index('\nbool loadAuthoritativeAppCatalog(', loader_start)
        loader = HOST[loader_start:loader_end]
        self.assertIn('RuntimeMemory::PsramTextStream json(kMaxCatalogBytes)', loader)
        self.assertIn('RuntimeMemory::PsramJsonAllocator allocator', loader)
        self.assertIn('JsonDocument document(&allocator)', loader)

        authoritative_start = HOST.index('bool loadAuthoritativeAppCatalog(')
        authoritative_end = HOST.index('\nbool appCatalogRefresh()', authoritative_start)
        authoritative = HOST[authoritative_start:authoritative_end]
        self.assertIn('return loadIndependentAppIndex(catalog);', authoritative)
        self.assertNotIn('refreshExternalGameBoy(', authoritative)

        # App Store no longer owns Wi-Fi bootstrap; shared HTTP reconnects.
        self.assertNotIn('bool connectSavedWifi()', HOST)
        saved_network = (ROOT / 'src/runtime/network/SavedNetworkConnection.cpp').read_text(encoding='utf-8')
        downloader = (ROOT / 'src/network/HttpDownloader.cpp').read_text(encoding='utf-8')
        self.assertIn('bool ensureSavedConnection(', saved_network)
        self.assertIn('RuntimeNetwork::ensureSavedConnection(kNetworkReadyTimeoutMs)', downloader)

        self.assertIn('RuntimeMemory::PsramTextStream json(kMaxCatalogBytes)', CATALOG_INDEX)
        self.assertIn('RuntimeMemory::PsramJsonAllocator allocator', CATALOG_INDEX)
        self.assertIn('RuntimeMemory::PsramJsonAllocator allocator', APP_MANIFEST)
        self.assertIn('JsonDocument doc(&allocator)', APP_MANIFEST)
        self.assertIn('RuntimeMemory::PsramJsonAllocator metadataAllocator', HOST)
        self.assertIn('JsonDocument metadata(&metadataAllocator)', HOST)
        self.assertIn('heap_caps_malloc_extmem_enable(1024);', MAIN)
        self.assertNotIn('heap_caps_malloc_extmem_enable(128);', MAIN)

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

    def test_launch_and_version_query_inspect_without_payload_hashing(self):
        self.assertIn('RuntimePackages::inspectInstalledAppPair(destination.c_str(), sidecar.c_str(), fileName)', HOST)
        self.assertIn('RuntimePackages::inspectInstalledAppPair(elf.c_str(), sidecar.c_str(), filename.c_str())', HOST)
        self.assertIn('Application metadata or executable is unavailable.', HOST)
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
