Import("env")
import os
from pathlib import Path
root = Path(env.subst("$PROJECT_DIR"))
if not (root / "src/runtime/packages/PackageExecutableAdmission.cpp").is_file():
    raise RuntimeError("CAM runtime requires pinned U1 480bf345 integrated with master; see docs/CAM_HEADLESS_RUNTIME.md")
# Reuse an installed ArduinoJson checkout; never download dependencies in a
# hardware build. CI may supply an isolated, explicitly pinned dependency.
json_include = os.environ.get("RISCRTE_ARDUINOJSON_INCLUDE")
if not json_include or not (Path(json_include) / "ArduinoJson.h").is_file():
    raise RuntimeError("Set RISCRTE_ARDUINOJSON_INCLUDE to an installed ArduinoJson 7 include directory")
env.Append(CPPPATH=[json_include])
# Local port headers must win even if the installed dependency directory also
# contains a header named Logging.h. ArduinoJson is the only dependency here.
env.Prepend(CPPPATH=[str(root / "ports/cam")])
env.BuildSources("$BUILD_DIR/cam-port", "$PROJECT_DIR/ports/cam", src_filter="+<*.cpp>")
env.BuildSources("$BUILD_DIR/sd-vfs", "$PROJECT_DIR/lib/NativeApps/src", src_filter="+<SdVfs.cpp>")
env.BuildSources("$BUILD_DIR/packages", "$PROJECT_DIR/src/runtime/packages", src_filter="+<*.cpp>")
env.BuildSources("$BUILD_DIR/providers", "$PROJECT_DIR/src/runtime/drivers", src_filter="+<InstalledProviderGraph.cpp> +<DeviceProviderExecutorV2.cpp> +<ProviderGraphV2.cpp> +<ProviderModuleV2.cpp> +<DriverPackage.cpp>")
env.BuildSources("$BUILD_DIR/elf-loader", "$PROJECT_DIR/lib/elf_loader/src", src_filter="+<esp_elf.c> +<esp_elf_adapter.c> +<esp_elf_symbol.c> +<esp_privileged_os_cpu.c> +<esp_privileged_elf.c> +<esp_privileged_imports.c> +<esp_privileged_manifest_imports.c> +<arch/esp_elf_xtensa.c> +<dlso/*.c> +<esp_elf_validate.c>")
env.BuildSources("$BUILD_DIR/native-core", "$PROJECT_DIR/src/native", src_filter="+<NativeStreamBridge.cpp> +<NativeSerialPortBridge.cpp> +<NativeAppMemory.cpp>")
env.BuildSources("$BUILD_DIR/streams", "$PROJECT_DIR/src/runtime/streams", src_filter="+<StreamRuntime.cpp> +<GnssRecordAdapter.cpp>")

if env.subst("$PIOENV") == "cam-headless-qualification":
    env.BuildSources("$BUILD_DIR/cam-qualification", "$PROJECT_DIR/test/hardware/cam", src_filter="+<*.cpp>")

# Pin the exact archive manifests for the lab-only idempotent install check.
# The ordinary manager still verifies every declared installed payload byte.
if env.subst("$PIOENV") == "cam-camera-experiment":
    import zipfile
    generated = Path(env.subst("$BUILD_DIR")) / "camera-package-identity"
    generated.mkdir(parents=True, exist_ok=True)
    declarations = ["#pragma once", "#include <stdint.h>"]
    for symbol, archive in (
        ("camProfileManifest", "driver-cam-ov3660-profile-0.1.0-xtensa-esp32s3.rte.zip"),
        ("camDriverManifest", "driver-camera-esp32s3-ov3660-0.1.6-xtensa-esp32s3.rte.zip"),
    ):
        with zipfile.ZipFile(root / "dist/packages" / archive) as package:
            if package.namelist().count(".package.json") != 1:
                raise RuntimeError("Expected one canonical package manifest")
            entry = package.getinfo(".package.json")
            if not 0 < entry.file_size <= 4096:
                raise RuntimeError("Lab package manifest exceeds bounded identity check")
            manifest = package.read(entry)
        declarations.append("static constexpr uint8_t " + symbol + "[]={" +
                            ",".join(str(byte) for byte in manifest) + "};")
    header = generated / "CameraPackageIdentity.h"
    contents = "\n".join(declarations) + "\n"
    if not header.exists() or header.read_text() != contents:
        header.write_text(contents)
    env.Prepend(CPPPATH=[str(generated)])
