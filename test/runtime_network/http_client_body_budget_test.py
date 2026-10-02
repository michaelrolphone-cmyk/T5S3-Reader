#!/usr/bin/env python3
"""Run the unchanged pinned Arduino HTTPClient body loop with the production adapter."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]).read_text()
start = source.index('int HTTPClient::writeToStreamDataBlock(')
end = source.index('\n/**', start)
body = source[start:end]
assert 'while(connected()' in body and '_client->available()' in body
with tempfile.TemporaryDirectory(prefix='u1-http-budget-') as directory:
    path = Path(directory)
    (path/'upstream_body.inc').write_text(body)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-DU1_REAL_HTTP_BODY', '-I'+str(path), '-I'+str(ROOT/'src'),
                    str(ROOT/'test/runtime_network/http_client_budget_test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True, env=os.environ.copy())
