#!/usr/bin/env python3
"""Prevent reintroducing the removed package-authentication implementation.

General TLS/OS cryptography and ordinary SHA/ABI/import checks are retained.
Historical prose is not executable dependency evidence and is not scanned here.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
removed_sources = {
    'PackageArchive.h',
    'PackageArchiveExtract.h',
    'PackageArchiveStage.h',
    'PackageArchiveVerification.h',
    'PackageDeviceCrypto.cpp',
    'PackageDeviceCrypto.h',
    'PackageDeviceDirectory.cpp',
    'PackageDeviceDirectory.h',
    'PackageDeviceExtract.cpp',
    'PackageDeviceExtract.h',
    'PackageDeviceInspection.cpp',
    'PackageDeviceInspection.h',
    'PackageDeviceInstaller.cpp',
    'PackageDeviceInstaller.h',
    'PackageDeviceProviderProfile.cpp',
    'PackageDeviceProviderProfile.h',
    'PackageDevicePublication.cpp',
    'PackageDevicePublication.h',
    'PackageDeviceSecurityFloor.cpp',
    'PackageDeviceSecurityFloor.h',
    'PackageDeviceStage.cpp',
    'PackageDeviceStage.h',
    'PackageProviderProfile.h',
    'PackageSecurityFloor.h',
    'PackageSignedProvenance.h',
    'PackageSignedTransaction.h',
    'PackageTrustPolicy.h',
}
for name in removed_sources:
    assert not (ROOT / "src/runtime/packages" / name).exists(), name
for name in ('scripts/build_risc_package.py', 'test/run_signed_package_experiment.sh',
             'docs/experimental/RISC_PACKAGE_FORMAT_SIGNED_V1.md'):
    assert not (ROOT / name).exists(), name
for directory in ('src/runtime/packages', 'src/runtime/drivers'):
    for path in (ROOT / directory).rglob('*'):
        if not path.is_file() or path.suffix not in ('.h', '.cpp', '.c', '.inc'):
            continue
        text = path.read_text()
        for forbidden in ('RISCPKG1', '.risc-auth', 'mbedtls_ecdsa_', 'mbedtls_ecp_',
                          'signerKeyId', 'signedImports', 'minimumSecurityVersion',
                          'addAuthenticatedPrivileged'):
            assert forbidden not in text, (path, forbidden)
# No production include or normal build/release command retains a deleted path.
for directory in ('src', 'lib/NativeApps', 'scripts', '.github/workflows', 'test'):
    for path in (ROOT / directory).rglob('*'):
        if not path.is_file() or path == Path(__file__).resolve() or path.suffix not in (
                '.h', '.cpp', '.c', '.inc', '.py', '.sh', '.yml', '.yaml'):
            continue
        text = path.read_text()
        for target in re.findall(r'#include\s*["<]([^">]+)[">]', text):
            assert Path(target).name not in removed_sources, (path, target)
        assert 'run_signed_package_experiment.sh' not in text, path
        assert 'build_risc_package.py' not in text, path
print('PASS: signing-only source/tools/gates absent; ordinary runtime admission remains separate')
