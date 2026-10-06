#!/usr/bin/env python3
"""Differential regression using checkout font app/HAL/SD/FatFs sources.

Pass the pinned ArduinoJson 7.4.2 src include directory. No network or source
snapshot is needed. --source /path/to/original/NativeFontBridge.cpp additionally
compares an original bridge with the checkout's reader-only negative control;
--original-hal can pair it with its original HalStorageVolume.cpp.
Builds and extracted definitions are temporary; production is never rewritten.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def definitions(bridge):
    """Extract complete live definitions, without reimplementing their bodies."""
    structs = bridge[bridge.index("struct ManifestFile"):bridge.index("bool active()")]
    start = bridge.index("bool computeCrc32(")
    end = bridge.index("t5_font_result_t installFamily(", start)
    validators = (ROOT / "src/FontInstaller.cpp").read_text()
    validators = validators[validators.index("bool FontInstaller::isValidFamilyName"):
                            validators.index("bool FontInstaller::ensureFamilyDir")]
    return structs + "\n" + validators + "\n" + bridge[start:end]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compare(reference, actual, label):
    assert reference.keys() == actual.keys(), label + ": scenario set changed"
    for name, expected in reference.items():
        observed = actual[name]
        # ONLY scheduling is excluded. Compare every provider request, returned
        # byte, position, error, cleanup, publication, familyInfo and app row.
        left = {k: v for k, v in expected.items() if k != "schedule"}
        right = {k: v for k, v in observed.items() if k != "schedule"}
        if left != right:
            assert left.keys() == right.keys(), f"{label}/{name}: report fields differ"
            for key in left:
                if left[key] != right.get(key):
                    if key == "trace":
                        for i, (a, b) in enumerate(zip(left[key], right[key])):
                            if a != b:
                                raise AssertionError(f"{label}/{name} trace[{i}]: {a!r} != {b!r}")
                    raise AssertionError(f"{label}/{name}: {key} differs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arduinojson", type=Path, help="ArduinoJson 7.4.2 src include directory")
    parser.add_argument("--source", type=Path, help="also compare an original NativeFontBridge.cpp")
    parser.add_argument("--original-hal", type=Path, help="compile this original HalStorageVolume.cpp for --source only")
    parser.add_argument("--transport", choices=("both", "x4", "t5"), default="both")
    parser.add_argument("--sanitizers", default="undefined", choices=("undefined", "address,undefined"))
    args = parser.parse_args()
    if args.original_hal and not args.source:
        parser.error("--original-hal requires --source")
    aj = args.arduinojson.resolve()
    assert '#define ARDUINOJSON_VERSION "7.4.2"' in (aj / "ArduinoJson/version.hpp").read_text()
    current = (ROOT / "src/native/NativeFontBridge.cpp").read_text()
    optimized_call = "deserializeJson(doc, reader)"
    assert current.count(optimized_call) == 1, "expected one checkout CatalogReader call"
    # This is the ENTIRE negative-control production change. Validators,
    # atomic publication, cleanup, HAL and provider remain current.
    control = current.replace(optimized_call, "deserializeJson(doc, file)")
    variants = [("control", control, False), ("cooperative", current, True),
                ("legacy-guard", current, False)]
    if args.source:
        original = args.source.resolve().read_text()
        assert optimized_call not in original, "--source must be the original scalar-reader bridge"
        variants.append(("original", original, False))
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    sanitizer = ["-fsanitize=" + args.sanitizers]
    compiler = subprocess.check_output(["c++", "--version"], text=True)
    # ArduinoJson proxy iterators trigger these GCC diagnostics even with
    # -isystem. Keep them visible; do not turn unrelated warnings into passes.
    warnings = [] if "clang" in compiler.lower() else ["-Wno-error=maybe-uninitialized"]
    if "clang" not in compiler.lower() and int(subprocess.check_output(["c++", "-dumpversion"], text=True).split(".")[0]) >= 13:
        warnings.append("-Wno-error=dangling-reference")
    with tempfile.TemporaryDirectory(prefix="font-catalog-") as temp:
        build = Path(temp)
        fixture = (ROOT / "test/storage_volume/runtime_test.cpp").read_text()
        (build / "card.inc").write_text(fixture[:fixture.index('#include "directory_iteration_test.inc"')])
        arduino = (ROOT / "test/storage_volume/stubs/Arduino.h").read_text()
        # Preserve the embedded 32-bit millis contract on LP64 hosts; the
        # Arduino and real SD provider continue sharing the same clock.
        arduino = arduino.replace("inline unsigned long millis() { return static_cast<unsigned long>(card_time); }",
                                  "inline uint32_t millis() { return static_cast<uint32_t>(card_time); }")
        arduino = arduino.replace("inline void delay(unsigned long n) { card_time += n; }",
                                  "extern unsigned hal_waits;\ninline void delay(unsigned long n) { ++hal_waits; card_time += n; }")
        (build / "Arduino.h").write_text(arduino)
        mmio = load_module("font_mmio_boundary", ROOT / "test/storage_volume/mmio_boundary.py")
        (build / "x4pro_mmio.h").write_text(mmio.header(ROOT))
        objects = []
        for source in ("Drivers/storage_fatfs/fatfs/ff.c", "Drivers/storage_fatfs/fatfs/ffunicode.c",
                       "test/storage_volume/os_cpu_fake.c", "Apps/font_manager.c"):
            obj = build / (Path(source).name + ".o")
            subprocess.run(["cc", "-std=c11", "-O1", "-g0", "-Wall", "-Wextra", "-Werror",
                            "-Wno-overflow", *sanitizer, "-pthread", "-D_XOPEN_SOURCE=700",
                            "-Isdk/driver", "-Ilib/NativeApps/include", "-c", source, "-o", str(obj)],
                           cwd=ROOT, env=env, check=True)
            objects.append(str(obj))
        for transport in (("x4", "t5") if args.transport == "both" else (args.transport,)):
            driver = "Drivers/x4pro_sd/driver.c" if transport == "x4" else "Drivers/t5s3_sd/driver.c"
            obj = build / "driver.o"
            subprocess.run(["cc", "-std=c11", "-O1", "-g0", "-Wall", "-Wextra", "-Werror",
                            "-Wno-overflow", *sanitizer, "-pthread", "-D_XOPEN_SOURCE=700", "-Isdk/driver",
                            "-I" + str(build), "-IDrivers/x4pro_board", "-c", driver, "-o", str(obj)],
                           cwd=ROOT, env=env, check=True)
            common = ["c++", "-std=c++17", "-O1", "-g0", "-Wall", "-Wextra", "-Werror", *warnings,
                      "-Wno-unused-function", "-Wno-overloaded-virtual", *sanitizer, "-pthread",
                      "-DBOARD_XTEINK_X4_PRO" if transport == "x4" else "-DBOARD_T5S3_PRO",
                      "-I" + str(build), "-Itest/storage_volume", "-Itest/storage_volume/stubs", "-Ilib/hal",
                      "-Isdk/driver", "-Ilib/NativeApps/include", "-Isrc/native", "-isystem", str(aj),
                      "-include", "Arduino.h"]
            if transport == "t5":
                common.append("-DTEST_SPI_TRANSPORT")
            reference = None
            for name, bridge, cooperative in variants:
                extracted = definitions(bridge)
                if name == "legacy-guard":
                    # Compile the real legacy call-site branch, while keeping
                    # this fixture's HAL volume TU board-specific. This is NOT
                    # a claim of runtime integration with legacy SdFat.
                    extracted = "#undef BOARD_XTEINK_X4_PRO\n#undef BOARD_T5S3_PRO\n" + extracted
                (build / "production_font.inc").write_text(extracted)
                binary = build / "font-catalog"
                subprocess.run([*common, "-DCOOPERATIVE_READER=" + str(int(cooperative)),
                                "test/native_apps/font_catalog_cooperation_test.cpp",
                                str(args.original_hal.resolve()) if name == "original" and args.original_hal else "lib/hal/HalStorageVolume.cpp",
                                str(obj), *objects, "-o", str(binary)], cwd=ROOT, env=env, check=True)
                run = subprocess.run([str(binary)], cwd=ROOT, env=env, text=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
                assert run.returncode == 0, f"{transport}/{name}: {run.stderr}"
                records = [json.loads(line) for line in run.stdout.splitlines()]
                if cooperative:
                    repeated = subprocess.run([str(binary)], cwd=ROOT, env=env, text=True,
                                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
                    assert repeated.returncode == 0, repeated.stderr
                    assert records == [json.loads(line) for line in repeated.stdout.splitlines()], "nondeterministic shared-clock trace"
                results = {record["case"]: record for record in records}
                assert len(records) == len(results), "duplicate scenario name"
                if reference is None:
                    reference = results
                else:
                    compare(reference, results, transport + "/" + name)
                launch = results["app-launch-64-desc127"]
                waits = launch["schedule"]["waits"]
                assert launch["read_calls"] == launch["read_bytes"] == 26259
                if cooperative:
                    # Shared clock/provider fixture adds one elapsed checkpoint
                    # to the isolated 820-yield budget result.
                    assert waits == 821 and waits < 26259 // 16
                else:
                    assert waits == 26259
                if cooperative:
                    changes = sum(reference[key]["schedule"]["provider_sleeps"] != record["schedule"]["provider_sleeps"]
                                  for key, record in results.items())
                    print(f"{transport}: provider scheduler counts differ in {changes} scenarios; "
                          "data/sector behavior remains identical", flush=True)
                provider_waits = launch["schedule"]["provider_sleeps"]
                print(f"{transport}/{name}: {len(results)} scenarios, launch scalar reads=26259, "
                      f"HAL waits={waits}, provider sleeps={provider_waits}", flush=True)
            print(f"{transport}: complete semantic/request/error/cleanup/row traces identical", flush=True)
    print("PASS: shared-clock app/provider parity; isolated budget unit 26259 reads -> 820 yields; not hardware latency")


if __name__ == "__main__":
    main()
