#!/usr/bin/env python3
"""Real ArduinoJson codec and complete production state loader, synthetic SD only."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
parser = argparse.ArgumentParser()
parser.add_argument('json_include', type=Path)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
ROOT = Path(os.environ.get('STATE_TEST_ROOT', Path(__file__).resolve().parents[2]))
HERE = Path(__file__).resolve().parent
codec = (ROOT / 'src/JsonSettingsIO.cpp').read_text()
start = codec.index('bool JsonSettingsIO::saveState(')
end = codec.index('// ---- CrossPointSettings', start)
source = '''#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <JsonSettingsIO.h>
#include <CrossPointState.h>
#include <Serialization.h>
#include <cassert>
#include <iostream>
#include <utility>
'''+codec[start:end]+(HERE / 'cases.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='state-json-') as tmp:
    cpp = Path(tmp) / 'test.cpp'
    cpp.write_text(source)
    binary = Path(tmp) / 'test'
    flags = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    includes = [HERE / 'stubs', ROOT / 'src', ROOT / 'lib/Serialization', args.json_include.resolve()]
    subprocess.run(flags + ['-I'+str(p) for p in includes] +
                   [str(ROOT / 'src/CrossPointState.cpp'), str(cpp), '-o', str(binary)], check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
