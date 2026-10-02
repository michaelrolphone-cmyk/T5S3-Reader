#!/usr/bin/env python3
"""Exercise the actual board poll body with a clock and installed-owner stub."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'lib/Board_T5S3/BoardT5S3.cpp').read_text()
body = source.split('bool isUsbConnected() {', 1)[1].split('\n}', 1)[0]
harness = r'''
#include <cassert>
#include <climits>
static unsigned long now = 10000;
static bool chargerConfigured = false;
unsigned long millis() { return now; }
namespace BoardPowerPort {
bool ready = false, configured = false, readable = true, external = true;
unsigned reads = 0, configurations = 0;
bool readyForActivation() { return ready; }
bool configure() { ++configurations; return configured; }
bool externalPower(bool* out) { ++reads; if (!readable) return false; *out = external; return true; }
}
bool isUsbConnected() { BODY
}
int main() {
  using namespace BoardPowerPort;
  assert(isUsbConnected()); assert(reads == 1);
  for (unsigned i = 0; i < 100; ++i) assert(isUsbConnected());
  now += 999; assert(isUsbConnected()); assert(reads == 1);
  ++now; external = false; assert(!isUsbConnected()); assert(reads == 2);
  now += 1000; external = true; assert(isUsbConnected()); assert(reads == 3);
  now += 1000; readable = false; assert(isUsbConnected()); assert(reads == 4);
  for (unsigned i = 0; i < 100; ++i) assert(isUsbConnected());
  assert(reads == 4);  // Failed reads are throttled and retain the last state.
  ready = true; assert(isUsbConnected()); assert(configurations == 1);
  now += 29999; assert(isUsbConnected()); assert(configurations == 1);
  ++now; configured = true; assert(isUsbConnected()); assert(configurations == 2);
  now += 30000; assert(isUsbConnected()); assert(configurations == 2);
  // Unsigned subtraction remains bounded when the clock wraps.
  now = ULONG_MAX - 499; assert(isUsbConnected()); const unsigned before = reads;
  now = 499; assert(isUsbConnected()); assert(reads == before);
  now = 500; assert(isUsbConnected()); assert(reads == before + 1);
}
'''.replace('BODY', body)
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'poll.cpp'
    binary = Path(directory) / 'poll'
    cpp.write_text(harness)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Board power polling: bounded reads/retries, failed-state retention, clock wrap PASS')
