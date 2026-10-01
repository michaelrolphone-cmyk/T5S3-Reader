#!/usr/bin/env python3
"""Real packer -> record -> index -> runtime immutable catalog contract."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test'))
import product_release_test as fixture

CPP = r'''
#include "runtime/packages/PackageOnlineCatalog.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream input(argv[1]);
  const std::string json((std::istreambuf_iterator<char>(input)), {});
  auto index = std::make_unique<RuntimePackages::IndependentDriverCatalog>();
  auto selected = std::make_unique<RuntimePackages::OnlinePackageCatalog>();
  if (!RuntimePackages::parseIndependentDriverCatalog(json.data(), json.size(), *index) ||
      !RuntimePackages::mergeOnlineCatalog(nullptr, *index, "xtensa-esp32s3", *selected)) return 3;
  for (size_t i = 0; i < selected->packageCount; ++i) {
    std::string url;
    if (!RuntimePackages::onlineArchiveUrl(selected->packages[i], selected->releases[i],
                                         "xtensa-esp32s3", url)) return 4;
    std::cout << url << '\n';
  }
}
'''

def main():
    case = fixture.ProductReleaseTests()
    case.setUp()
    try:
        record = case.record()
        index = fixture.update_index({'schema': 1, 'firmware': None, 'apps': [], 'drivers': []},
                                     'drivers', record)
        fixture.stage_app(case.root)
        app = fixture.build_record('apps', 'clock', '1.2.3', case.root)
        index = fixture.update_index(index, 'apps', app)
        with tempfile.TemporaryDirectory(prefix='package-catalog-roundtrip-') as temp:
            path = Path(temp)
            source, binary, metadata = path/'test.cpp', path/'test', path/'index.json'
            source.write_text(CPP)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                            '-Werror', '-I'+str(ROOT/'src'), str(source), '-o', str(binary)], check=True)
            metadata.write_text(fixture.serialize_index(index))
            result = subprocess.run([str(binary), str(metadata)], check=True, capture_output=True, text=True)
            assert set(result.stdout.splitlines()) == {record['url'], app['url']}
            # The producer's manifest/identity must remain joined at device intake.
            index['drivers'][0]['manifest']['version'] = '2.0.2'
            metadata.write_text(json.dumps(index))
            assert subprocess.run([str(binary), str(metadata)]).returncode == 3
        print('PASS: real ZIP/record/index round-trip reaches exact immutable runtime URL')
    finally:
        case.tearDown()

if __name__ == '__main__':
    main()
