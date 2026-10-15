"""Target-correct ESP32-S3 compiler discovery without an Arduino dependency.

ESP-IDF 6.1's unified installation includes xtensa-esp32s3-elf-gcc/g++
wrappers. Use those wrappers: bare xtensa-esp-elf-gcc defaults to a different
Xtensa configuration, including big-endian output. Historical GCC8 packages
remain supported only as an explicit override or a last-resort fallback.
"""
import os
from pathlib import Path
import shutil

TARGET_CC = "xtensa-esp32s3-elf-gcc"


def _existing_compiler(value):
    value = os.fspath(value)
    found = shutil.which(value) or str(Path(value).expanduser())
    path = Path(found)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise FileNotFoundError("ESP32-S3 compiler is not executable: " + value)
    # Do not resolve symlinks: the executable name can select the CPU config.
    if path.name in ("xtensa-esp-elf-gcc", "xtensa-esp-elf-gcc.exe"):
        suffix = ".exe" if path.suffix == ".exe" else ""
        wrapper = path.with_name(TARGET_CC + suffix)
        if not wrapper.is_file() or not os.access(wrapper, os.X_OK):
            raise ValueError("Bare unified Xtensa GCC needs its ESP32-S3 wrapper: " + str(wrapper))
        path = wrapper
    return str(path.absolute())


def compiler_path(explicit=None, *, environment="NATIVE_APP_CC"):
    """Honor the override, exported native toolchain, then legacy tool package.

    No downloads, SDK-header guessing, firmware build, or package installation.
    An invalid explicit override fails instead of silently selecting another SDK.
    """
    requested = explicit or os.environ.get(environment)
    if requested:
        return _existing_compiler(requested)
    found = shutil.which(TARGET_CC)
    if found:
        return _existing_compiler(found)
    # Some exported installations expose only the unified name on PATH; use
    # its sibling target wrapper, never the generic compiler without config.
    found = shutil.which("xtensa-esp-elf-gcc")
    if found:
        return _existing_compiler(found)
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    legacy = core / "packages/toolchain-xtensa-esp32s3/bin" / TARGET_CC
    if legacy.is_file():
        return _existing_compiler(legacy)
    raise FileNotFoundError(
        "ESP32-S3 compiler unavailable. Export ESP-IDF 6.1's toolchain "
        "or set " + environment + " to xtensa-esp32s3-elf-gcc."
    )
