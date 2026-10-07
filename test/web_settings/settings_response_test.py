#!/usr/bin/env python3
"""Compile the complete production settings handler against real ArduinoJson."""
import argparse
from pathlib import Path
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('arduino_json', type=Path)
parser.add_argument('--source-ref', help='Use original handler for a negative control')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source_path = 'src/network/CrossPointWebServer.cpp'
source = (subprocess.check_output(['git', 'show', f'{args.source_ref}:{source_path}'], cwd=root, text=True)
          if args.source_ref else (root / source_path).read_text())
start = source.index('void CrossPointWebServer::handleGetSettings() const {')
end = source.index('\nvoid ', start + 1)
with tempfile.TemporaryDirectory(prefix='web-settings-') as directory:
    build = Path(directory)
    (build / 'handler.inc').write_text(source[start:end])
    current = (root / source_path).read_text()
    abort_start = current.index('  void abortResponse() {')
    abort_end = current.index('\n  }', abort_start) + len('\n  }')
    (build / 'abort_response.inc').write_text(current[abort_start:abort_end])
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-I' + str(root / 'test/web_settings/stubs'), '-I' + str(args.arduino_json.resolve()),
               '-I' + str(root / 'src'), '-I' + str(build),
               str(root / 'test/web_settings/settings_response_test.cpp'), '-o', str(build / 'test')]
    if args.sanitize:
        command[1:1] = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(build / 'test')], check=True, timeout=30)
