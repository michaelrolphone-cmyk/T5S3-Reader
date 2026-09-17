#!/usr/bin/env python3
"""Prevent regressions from the live App Store back to split ELF/JSON swaps.

The template transaction's fault-injection tests exercise rename/delete states;
firmware board builds compile this real HalStorage/mbedTLS adapter. This source
contract additionally ensures the live API is wired to those tested paths.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOST = (ROOT / 'src/native/NativeAppHost.cpp').read_text(encoding='utf-8')
ADAPTER = (ROOT / 'src/native/AppPackageInstaller.cpp').read_text(encoding='utf-8')


class LiveInstallContract(unittest.TestCase):
    def test_live_installer_is_digest_verified_and_publishes_pair(self):
        start = HOST.index('bool appCatalogDownload(uint32_t index)')
        end = HOST.index('\nbool installedRefresh()', start)
        install = HOST[start:end]
        for required in ('RuntimePackages::safePackageEntryName',
                         'metadata["size_bytes"].as<unsigned>() != asset.size',
                         'metadata["sha256"].is<const char*>()',
                         'RuntimePackages::recoverAppPair(',
                         'RuntimePackages::clearAppStage(',
                         'RuntimePackages::verifyAppPair(temporary.c_str()',
                         'RuntimePackages::publishAppPair('):
            self.assertIn(required, install)
        self.assertLess(install.index('recoverAppPair('), install.index('clearAppStage('))
        self.assertLess(install.index('verifyAppPair(temporary.c_str()'),
                        install.index('publishAppPair('))
        for obsolete in ('Storage.remove(destination.c_str())',
                         'Storage.rename(destination.c_str(), backup.c_str())',
                         'Storage.rename(sidecar.c_str(), backupJson.c_str())'):
            self.assertNotIn(obsolete, install)

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


if __name__ == '__main__':
    unittest.main()
