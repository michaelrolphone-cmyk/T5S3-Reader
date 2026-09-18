#!/usr/bin/env python3
"""Guard the production Driver Manager handoff, in addition to C++ transactions.

This source inspection deliberately does not claim physical-driver behavior. It
catches accidentally returning to the old unversioned publication helper or
silently deleting an interrupted staging directory in the production path.
"""
from pathlib import Path

source = (Path(__file__).resolve().parents[2] /
          'src/runtime/drivers/DriverPackage.cpp').read_text()
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
assert 'validat' in installer and 'verify(paths.stage, observed)' in installer
assert 'Installed driver' in installer and 'activation unchanged' in installer
print('Driver Manager production callsite uses typed publication and preserves stale stage')
