"""Pack the ordinary CAM utility ZIP expected by the CAM port's app entry test."""
import hashlib
import json
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
app = json.loads((root / "Apps/camera_utility.json").read_text())
if app["id"] != "camera_utility" or app["version"] != "0.1.0":
    raise ValueError("CAM pilot package identity changed; review the test contract")
names = ("camera_utility.elf", "camera_utility.json")
files = {name: (root / "dist/apps" / name).read_bytes() for name in names}
if not 0 < len(files[names[0]]) < 256 * 1024:
    raise ValueError("CAM utility ELF out of bounds")
entries = [{"name": name, "size_bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "executable": name.endswith(".elf")}
           for name, data in files.items()]
manifest = {"schema": 1, "kind": "application", "id": app["id"],
            "version": app["version"], "artifact": names[0],
            "architecture": "xtensa-esp32s3", "min_runtime_api": 2,
            "entries": entries, "requires": [{"capability": "camera.capture", "min_api": 1}]}
out = root / "dist/release-app-packages/application-camera_utility-0.1.0-xtensa-esp32s3.rte.zip"
out.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_STORED) as archive:
    for name, data in ((".package.json", json.dumps(manifest, separators=(",", ":")).encode()), *files.items()):
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_STORED
        archive.writestr(info, data)
print(out)
