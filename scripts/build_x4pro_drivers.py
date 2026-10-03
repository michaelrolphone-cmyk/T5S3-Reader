#!/usr/bin/env python3
"""Build Xteink X4 Pro capability drivers as Xtensa provider-v2 ELFs."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from normalize_xtensa_relocations import normalize
from validate_xtensa_relative_targets import validate as validate_relative_targets

def compiler():
    env = os.environ.get("NATIVE_DRIVER_CC")
    if env:
        return env
    found = shutil.which("xtensa-esp32s3-elf-gcc")
    if found:
        return found
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    return str(core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")

CC = compiler()
PACKAGES = [
    "x4pro_i2c", "x4pro_panel", "x4pro_gt911", "x4pro_buttons",
    "x4pro_frontlight", "x4pro_battery", "x4pro_sd",
]

def check_i2c_sdk_contract():
    """Check existing CPU ABI declarations, without adding loader privileges."""
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    sdk = Path(os.environ.get("ESP32S3_SDK", core / "packages/framework-arduinoespressif32/tools/sdk/esp32s3"))
    if not (sdk / "include/freertos/include/freertos/FreeRTOS.h").is_file():
        if os.environ.get("CI"):
            raise ValueError("Pinned SDK missing for X4 I2C CPU ABI contract check")
        print("X4 I2C SDK declaration check NOT RUN: pinned SDK unavailable locally")
        return
    include = [sdk / "qio_opi/include", sdk / "include/newlib/platform_include"]
    include += sorted(p for p in (sdk / "include").rglob("include") if p.is_dir())
    include += [sdk / "include/soc/esp32s3", sdk / "include/xtensa/esp32s3/include",
                sdk / "include/freertos/port/xtensa/include", sdk / "include/freertos/include/esp_additions",
                sdk / "include/freertos/include/esp_additions/freertos", sdk / "include/esp_rom/include/esp32s3"]
    # GCC14 diagnoses the pinned SDK's repeated IRAM_ATTR inline declarations
    # with different generated section names. This is a SDK attribute warning,
    # not a calling-convention/type mismatch. Keep all type errors and static
    # assertions fatal; production driver compiler flags are unchanged.
    subprocess.run([CC, "-std=gnu11", "-fsyntax-only", "-Werror", "-Wno-error=attributes",
                    "-DCONFIG_IDF_TARGET_ESP32S3=1",
                    *["-I" + str(p) for p in include],
                    str(ROOT / "test/drivers/x4pro_i2c_sdk_contract.c")], check=True)
    print("X4 I2C pinned SDK declaration check: PASS")


def build_one(name):
    if name in ("x4pro_i2c", "x4pro_sd"):
        check_i2c_sdk_contract()
    source = ROOT / "Drivers" / name
    manifest = json.loads((source / "manifest.json").read_text())
    output = ROOT / "dist" / "experimental" / manifest["id"]
    output.mkdir(parents=True, exist_ok=True)
    elf = output / "driver.elf"
    # CI run 37048644826 confirmed that pinned Linux Xtensa ld 2.35.1 links
    # the enlarged FAT32 provider at -O2 with relaxation disabled. Keep the
    # workaround scoped; all other providers retain their ordinary flags.
    # Explicit CI profile: GCC 14 still asserts for the panel at -Os;
    # -O2 with relaxation disabled builds every provider. Keep the older
    # default for other callers until they opt into the pinned toolchain.
    profile = os.environ.get("RISCRTE_X4_LINK_PROFILE", "legacy")
    if profile not in ("legacy", "esp14-no-relax"):
        raise ValueError("Unknown X4 linker profile")
    storage = name in ("x4pro_sd", "t5s3_sd")
    stable_link = storage or profile == "esp14-no-relax"
    optimization = "-O2" if stable_link else "-Os"
    link_flags = ["-Wl,--no-relax"] if stable_link else []
    # Loop induction optimization pre-biases frame by -12000 for the UC
    # transfer loop. That legal compiler transform emits an ELF-relative
    # pointer outside mapped sections, which our loader correctly refuses.
    # Keep the pointer based inside frame rather than weakening relocation.
    compile_flags = ["-fno-ivopts"] if profile == "esp14-no-relax" and name == "x4pro_panel" else []
    subprocess.run([
        CC, "-std=c11", optimization, *compile_flags,
        "-fPIC", "-mtext-section-literals", "-mlongcalls",
        "-fvisibility=hidden", "-fno-builtin", "-nostdlib", "-nostartfiles", "-shared",
        "-I" + str(ROOT / "sdk/driver"), "-I" + str(ROOT / "Drivers/x4pro_board"),
        "-Wl,--hash-style=sysv", "-Wl,--exclude-libs,ALL", *link_flags,
        str(source / "driver.c"),
        *([str(ROOT / "Drivers/storage_fatfs/fatfs/ff.c"),
           str(ROOT / "Drivers/storage_fatfs/fatfs/ffunicode.c")] if storage else []),
        "-lgcc", "-o", str(elf),
    ], check=True)
    readelf = CC.replace("gcc", "readelf")
    sections = subprocess.check_output([readelf, "-S", str(elf)], text=True)
    if ".rela.plt" in sections or ".rela.dyn" in sections:
        try:
            normalize(elf)
        except ValueError as exc:
            if "Missing required section" not in str(exc):
                raise
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    validate_relative_targets(elf)
    exported = {fields[7] for line in symbols.splitlines()
                if len(fields := line.split()) >= 8 and fields[4] == "GLOBAL"
                and fields[6] != "UND" and fields[3] == "FUNC"}
    if exported != {"t5_driver_get"}:
        raise ValueError(f"{name} exports {exported}")
    payload = elf.read_bytes()
    if payload[:7] != b"\x7fELF\x01\x01\x01" or int.from_bytes(payload[18:20], "little") != 94:
        raise ValueError(f"{name} is not an Xtensa shared object")
    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{manifest['id']} {len(payload)} {manifest['sha256']}")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", choices=PACKAGES,
                        help="build only one source directory for a release plan")
    args = parser.parse_args()
    for name in ([args.source] if args.source else PACKAGES):
        build_one(name)

if __name__ == "__main__":
    main()
