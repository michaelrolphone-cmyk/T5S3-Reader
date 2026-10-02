#!/usr/bin/env python3
"""Build the GT911 raw-touch provider as an independent provider-v2 ELF."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

from native_app_symbols import firmware_exports, privileged_os_cpu_exports, validate_imports
from probe_usb_controller_esp32s3 import compile_target, tool
from normalize_xtensa_relocations import normalize

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Drivers/gt911_touch"
OUTPUT = ROOT / "dist/experimental/gt911-touch"


def build(cc=None):
    manifest = json.loads((SOURCE / "manifest.json").read_text())
    required = {
        "type": "driver",
        "id": "gt911-touch",
        "driver_abi": 2,
        "architecture": "xtensa-esp32s3",
        "file_name": "driver.elf",
        "requires": [
            {"capability": "i2c.bus", "api": 1},
            {"capability": "platform.clock", "api": 1},
        ],
        "provides": [{"capability": "input.touch.raw", "api": 1}],
        "status": "experimental-unpublished",
    }
    if any(manifest.get(key) != value for key, value in required.items()):
        raise ValueError("Invalid GT911 touch provider manifest")

    # Use the firmware's exact FreeRTOS configuration for generic mutex ABI.
    subprocess.run(['pio', 'run', '-e', 't5s3-pro', '-t', 'compiledb'],
                   cwd=ROOT, check=True)
    entries = json.loads((ROOT / 'compile_commands.json').read_text())
    matched = [entry for entry in entries if
               Path(entry['file']).as_posix().endswith('src/native/NativeUsbBridge.cpp')]
    if len(matched) != 1:
        raise ValueError('Missing unique target compilation configuration')
    entry = matched[0]
    args = list(entry['arguments']) if 'arguments' in entry else shlex.split(entry['command'])
    if cc:
        raise ValueError('GT911 uses the compiler from the target compilation database')
    cc = str(tool(Path(args[0]), 'gcc'))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    obj = OUTPUT / 'driver.o'
    compile_target(args, entry, SOURCE / 'driver.c', obj, c_compiler=True,
                   extra=('-Wall', '-Wextra', '-Werror', '-mtext-section-literals', '-mlongcalls'))
    elf = OUTPUT / 'driver.elf'
    subprocess.run([
        cc, '-shared', '-nostdlib', '-nostartfiles',
        '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
        str(obj), '-lgcc', '-o', str(elf),
    ], check=True)
    normalize(elf)

    readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exported = {
        fields[7] for line in symbols.splitlines()
        if len(fields := line.split()) >= 8 and fields[4] == "GLOBAL"
        and fields[6] != "UND" and fields[3] == "FUNC"
    }
    if exported != {"t5_driver_get"}:
        raise ValueError("GT911 provider must export only t5_driver_get: " + repr(exported))
    # Generic libc/CPU helpers are permitted exactly as for other providers.
    # GT911 register I/O flows only through the i2c.bus dependency.
    validate_imports(
        symbols,
        {name for name in firmware_exports(ROOT) if not name.startswith("t5_")} |
        (privileged_os_cpu_exports(ROOT) & {
            'xQueueCreateMutex', 'xQueueSemaphoreTake', 'xQueueGenericSend', 'vQueueDelete'}),
    )

    payload = elf.read_bytes()
    if (
        not 52 <= len(payload) <= 256 * 1024
        or payload[:7] != b"\x7fELF\x01\x01\x01"
        or int.from_bytes(payload[16:18], "little") != 3
        or int.from_bytes(payload[18:20], "little") != 94
    ):
        raise ValueError("Invalid Xtensa GT911 provider ELF")

    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("GT911 input.touch.raw provider built with i2c.bus register I/O")
    return elf


if __name__ == "__main__":
    build()
