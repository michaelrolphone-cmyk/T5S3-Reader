#!/usr/bin/env python3
"""Measure actual target-ABI provider structures without executing target code."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('toolchain', type=Path, help='directory containing xtensa-esp32s3-elf-g++ and nm')
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
args = parser.parse_args()
root = args.source_root.resolve()
source = (root / 'src/runtime/drivers/InstalledProviderGraph.cpp').read_text()
frames = source[source.index('struct Root {'):source.index('\nbool providerFail(')]
depth = re.search(r'constexpr size_t kMaxRegistrationDepth = (\d+);', source)
body = '''#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <memory>
extern "C" size_t strnlen(const char*,size_t);
#include "runtime/drivers/InstalledSerialInventory.h"
#include "runtime/drivers/ProviderOwnedSpecV2.h"
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include "runtime/packages/PackageUseGate.h"
#include "runtime/packages/PackageOrdinaryStage.h"
#include "runtime/packages/InstalledProviderRootScan.h"
using namespace RuntimePackages;
constexpr size_t kMaxProviders=RuntimeProviders::GraphV2::kMaxModules;
'''
if depth:
    body += 'constexpr size_t kMaxRegistrationDepth=' + depth.group(1) + ';\n'
body += frames + '\nextern "C" {\n'
structures = {
    'Graph': 'RuntimeProviders::GraphV2',
    'OwnedNode': 'RuntimeProviders::OwnedNodeV2',
    'Imports': 'RuntimeProviders::OwnedNodeV2::ImportStorage',
    'RegistrationFrame': 'RegistrationFrame',
    'ProviderAncestry': 'ProviderAncestry',
    'SerialInventory': 'RuntimeInstalledProviders::InstalledSerialInventory',
    'PackageUseGate': 'RuntimePackages::PackageUseGate',
    'OrdinaryPlan': 'RuntimePackages::OrdinaryPackagePlan',
}
if 'struct ProviderMatches {' in frames:
    structures.update(LinkedCandidate='ProviderMatches::Match', MatchesStackOwner='ProviderMatches')
for name, structure in structures.items():
    body += f'char size_{name}[sizeof({structure})];\n'
body += 'char size_Pinned[kMaxProviders*96];\nchar size_DiscoveryCandidateTable[kMaxProviders*(64+64+4)];\n}\n'
with tempfile.TemporaryDirectory() as directory:
    cpp, obj = Path(directory)/'layout.cpp', Path(directory)/'layout.o'
    cpp.write_text(body)
    subprocess.run([str(args.toolchain/'xtensa-esp32s3-elf-g++'), '-std=c++17',
                    '-I'+str(root/'src'), '-I'+str(root/'sdk/driver'),
                    '-c', str(cpp), '-o', str(obj)], check=True)
    symbols = subprocess.check_output([str(args.toolchain/'xtensa-esp32s3-elf-nm'), '-S', str(obj)], text=True)
    result = {match.group(2): int(match.group(1), 16) for match in re.finditer(
        r'^[0-9a-f]+ ([0-9a-f]+) [A-Za-z] size_(\w+)$', symbols, re.M)}
    print(json.dumps(result, indent=2, sort_keys=True))
