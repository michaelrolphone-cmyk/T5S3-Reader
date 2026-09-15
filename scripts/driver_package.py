"""Strict package contract shared by the driver builder and offline installer."""
import hashlib
import json
import re
import struct

MAX_ELF_BYTES = 256 * 1024
REQUIRES = [{"capability": name, "api": 1} for name in ("kernel.serial", "kernel.power", "kernel.clock")]
PROVIDES = [{"capability": "position.gnss", "api": 1}]

def validate_manifest(data, *, packaged=False):
    expected = {"type": "driver", "id": "gps-nmea", "driver_abi": 1,
                "architecture": "xtensa-esp32s3", "file_name": "driver.elf"}
    for key, value in expected.items():
        if type(data.get(key)) is not type(value) or data[key] != value:
            raise ValueError(f"Invalid driver manifest {key}")
    if not isinstance(data.get("version"), str) or not re.fullmatch(r"\d+\.\d+\.\d+", data["version"]):
        raise ValueError("Invalid driver version")
    if data.get("requires") != REQUIRES or data.get("provides") != PROVIDES:
        raise ValueError("Unsupported driver capability contract")
    if packaged:
        if not re.fullmatch(r"[a-f0-9]{64}", str(data.get("sha256", ""))):
            raise ValueError("Missing ELF SHA-256")
        if type(data.get("size_bytes")) is not int or not 52 <= data["size_bytes"] <= MAX_ELF_BYTES:
            raise ValueError("Invalid ELF size")
    return data

def validate_payload(manifest, payload):
    validate_manifest(manifest, packaged=True)
    if len(payload) != manifest["size_bytes"] or hashlib.sha256(payload).hexdigest() != manifest["sha256"]:
        raise ValueError("Driver ELF integrity mismatch")
    if payload[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<HH", payload, 16) != (3, 94):
        raise ValueError("Expected Xtensa ELF32 little-endian shared object")
    return manifest

def read_json(payload):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"Duplicate manifest key: {key}")
            result[key] = value
        return result
    if len(payload) > 4096:
        raise ValueError("Driver manifest exceeds 4096 bytes")
    result = json.loads(payload, object_pairs_hook=unique)
    if not isinstance(result, dict):
        raise ValueError("Driver manifest must be an object")
    return result
