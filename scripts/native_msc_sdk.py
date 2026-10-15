"""Pinned native IDF headers for the ordinary, source-owned MSC provider."""
import os
from pathlib import Path
import subprocess

IDF_COMMIT = "fff9895c82d744c7237be8847347bdd1b07c6643"
COMPILER_DISTRIBUTION = "esp-15.2.0_20251204"
ROOT = Path(__file__).resolve().parents[1]


def output(argv):
    return subprocess.check_output(list(map(str, argv)), text=True).strip()


def validate_compiler(cc, native=False):
    macros = subprocess.check_output([cc, "-dM", "-E", "-x", "c", "-"], input="", text=True)
    if "#define __XTENSA_EL__ 1" not in macros or "#define __XTENSA_WINDOWED_ABI__ 1" not in macros:
        raise ValueError("MSC compiler must target little-endian windowed ESP32-S3")
    if native and (output([cc, "-dumpmachine"]) != "xtensa-esp-elf" or
                   output([cc, "-dumpfullversion"]) != "15.2.0" or
                   COMPILER_DISTRIBUTION not in output([cc, "--version"])):
        raise ValueError("Native MSC requires pinned " + COMPILER_DISTRIBUTION)


def native_headers(idf, out):
    idf = Path(idf or os.environ.get("IDF_PATH", "")).resolve()
    if not (idf / "components").is_dir():
        raise ValueError("Set IDF_PATH or --idf to the pinned ESP-IDF 6.1 checkout")
    if output(["git", "-C", idf, "rev-parse", "HEAD"]) != IDF_COMMIT:
        raise ValueError("Native MSC requires ESP-IDF " + IDF_COMMIT)
    if output(["git", "-C", idf, "status", "--porcelain", "--untracked-files=no"]):
        raise ValueError("Native MSC refuses modified ESP-IDF sources")
    # Only target/header options: this ordinary ELF never imports an RTOS ABI.
    config = out / "native-config"
    config.mkdir()
    (config / "sdkconfig.h").write_text(
        '#pragma once\n#define CONFIG_IDF_TARGET_ESP32S3 1\n'
        '#define CONFIG_IDF_TARGET_ARCH_XTENSA 1\n'
        '#define CONFIG_IDF_TARGET "esp32s3"\n'
        '#define CONFIG_HAL_DEFAULT_ASSERTION_LEVEL 0\n'
        '#define CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE 1\n')
    paths = ["esp_common/include", "esp_hw_support/include", "esp_hw_support/etm/include", "esp_driver_gpio/include",
             "esp_hal_gpio/include", "esp_hal_gpio/esp32s3/include", "esp_hal_usb/include",
             "esp_hal_usb/esp32s3/include", "hal/include", "hal/platform_port/include",
             "soc/include", "soc/esp32s3/include", "soc/esp32s3/register",
             "esp_rom/include", "esp_rom/esp32s3/include", "esp_rom/esp32s3",
             "esp_system/include"]
    includes = [ROOT / "sdk/compat/msc-idf61", config]
    includes += [idf / "components" / p for p in paths if (idf / "components" / p).is_dir()]
    return idf, includes, idf / "components/soc/esp32s3/ld/esp32s3.peripherals.ld"
