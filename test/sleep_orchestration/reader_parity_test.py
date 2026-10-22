#!/usr/bin/env python3
"""Compare actual Reader entrypoint side effects against the exact durable base.

Host fixtures model callbacks, not hardware; the real production orchestrator is
compiled into the candidate. No baseline source is copied into production.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = "9c8783d6f20659a514bfaeb3847a77c7213dc4f6"

def entrypoints(source):
    start = source.index("static void resumeAfterSleepRefused()")
    end = source.index("// Set by activities", start)
    return source[start:end]

MAIN = r'''
int main() {
  for (flags = 0; flags < (1u << 14); ++flags) {
    for (unsigned path = 0; path < 4; ++path) {
      calls.clear();
      SETTINGS.sleepScreen = flag(2) ? CrossPointSettings::DIGITAL_CLOCK : 0;
      SETTINGS.resumeReaderOnBoot = flag(12);
      APP_STATE.lastSleepFromReader = flag(5);
      APP_STATE.openEpubPath = flag(13) ? "book.epub" : "";
      switch (path) {
        case 0: enterDeepSleep(); break;
        case 1: enterDeepSleepKeepingScreen(true); break;
        case 2: enterDeepSleepKeepingScreen(false); break;
        case 3: enterPowerOffKeepingScreen("ignored"); break;
      }
      std::printf("%u:%u:%d:%s\n", flags, path, APP_STATE.lastSleepFromReader, calls.c_str());
    }
  }
}
'''

def run(source, board, temporary, name, sanitize):
    file = temporary / (name + ".cpp")
    binary = temporary / name
    file.write_text((ROOT / "test/sleep_orchestration/reader_fixture.h").read_text() + entrypoints(source) + MAIN)
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "src"), str(file),
               str(ROOT / "src/runtime/power/SleepOrchestration.cpp"), "-o", str(binary)]
    if board:
        command.append("-DBOARD_T5S3_PRO")
    if sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    return subprocess.check_output([str(binary)], timeout=30)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    baseline = subprocess.check_output(["git", "show", BASE + ":src/main.cpp"], cwd=ROOT, text=True)
    current = (ROOT / "src/main.cpp").read_text()
    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        for board in (False, True):
            before = run(baseline, board, temporary, "base", args.sanitize)
            after = run(current, board, temporary, "candidate", args.sanitize)
            if before != after:
                for expected, actual in zip(before.splitlines(), after.splitlines()):
                    if expected != actual:
                        raise AssertionError(f"board={board}: expected {expected!r}; got {actual!r}")
                raise AssertionError("entrypoint trace lengths differ")
            print(f"Reader production entrypoint parity: {'T5' if board else 'non-T5'}, 65,536 traces PASS")

if __name__ == "__main__":
    main()
