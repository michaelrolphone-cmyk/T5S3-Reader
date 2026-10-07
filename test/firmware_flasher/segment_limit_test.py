#!/usr/bin/env python3
"""Exercise the full production flasher with synthetic images and host-only I/O."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def make_image(segments, sha):
    image = bytearray(24)
    image[0], image[1], image[23] = 0xE9, segments, int(sha)
    checksum = 0xEF
    for index in range(segments):
        payload = bytearray(65536 if index == 0 else 4)
        if index == 0:
            marker = b"RISCRTE_BOARD_ID:fixture"
            payload[:len(marker)] = marker
        image += struct.pack("<II", 0x3C010000 + index * 65536, len(payload)) + payload
        for value in payload:
            checksum ^= value
    image += bytes(((len(image) + 16) & ~15) - len(image))
    image[-1] = checksum
    if sha:
        image += hashlib.sha256(image).digest()
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT,
                        help="Compile original source from another checkout for a negative control")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="firmware-segments-") as directory:
        build = Path(directory)
        for segments in (1, 16, 17, 255):
            for sha in (False, True):
                (build / f"{segments}-{'sha' if sha else 'xor'}.bin").write_bytes(make_image(segments, sha))
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror"]
        if args.sanitize:
            command += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += ["-I", str(HERE / "stubs"), "-I", str(args.source_root / "src/network"),
                    str(HERE / "segment_limit_test.cpp"),
                    str(args.source_root / "src/network/FirmwareFlasher.cpp"),
                    "-lcrypto", "-o", str(build / "test")]
        subprocess.run(command, check=True, timeout=90)
        subprocess.run([str(build / "test"), str(build)], check=True, timeout=30)


if __name__ == "__main__":
    main()
