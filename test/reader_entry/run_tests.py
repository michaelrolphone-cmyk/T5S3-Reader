#!/usr/bin/env python3
"""Compile production Reader entry/coordinator and focused activity handoffs."""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def run(command):
    subprocess.run([str(arg) for arg in command], check=True, timeout=45)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", default="all", choices=("all", "state", "app", "coordinator", "activities", "boot", "retained", "navigation", "x4"))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="reader-entry-test-") as directory:
        temp = Path(directory)
        flags = ["-Wall", "-Wextra", "-Werror", "-g"]
        includes = ["-I" + str(HERE / "stubs"), "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "lib/NativeApps/include"), "-I" + str(ROOT / "lib/hal")]
        if args.case in ("all", "state"):
            binary = temp / "state"
            run(["c++", "-std=c++17", *flags, *includes, HERE / "state_test.cpp", "-o", binary])
            run([binary])
        if args.case in ("all", "app", "coordinator"):
            app = temp / "default.o"
            run(["cc", "-std=c11", *flags, *includes, "-c", ROOT / "Apps/default.c", "-o", app])
        if args.case in ("all", "app"):
            binary = temp / "default-app"
            run(["cc", "-std=c11", *flags, *includes, HERE / "default_app_test.c", app, "-o", binary])
            run([binary])
        if args.case in ("all", "coordinator"):
            binary = temp / "coordinator"
            run(["c++", "-std=c++17", *flags, *includes, "-include", HERE / "stubs/host_declarations.h",
                 HERE / "coordinator_test.cpp", ROOT / "src/native/NativeReaderEntry.cpp", app, "-o", binary])
            for case in ("normal", "load_error", "unload_error", "resume_error", "startup_handoff", "retained"):
                run([binary, case])
        if args.case in ("all", "x4"):
            run(["python3", HERE / "x4_integration_test.py"])
        if args.case in ("all", "navigation"):
            run(["python3", HERE / "navigation_resume_test.py"])
        if args.case in ("all", "retained"):
            run(["python3", HERE / "retained_session_test.py"])
        if args.case in ("all", "boot"):
            run(["python3", HERE / "boot_dispatch_test.py"])
        if args.case in ("all", "activities"):
            run(["python3", HERE / "activity_handoff_test.py"])


if __name__ == "__main__":
    main()
