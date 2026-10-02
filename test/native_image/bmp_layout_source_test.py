#!/usr/bin/env python3
"""Ensure probing and pixel decoding share the checked BMP layout parser."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "src/native/NativeImageBridge.cpp").read_text(encoding="utf-8")
layout = (root / "src/native/BmpLayout.h").read_text(encoding="utf-8")

assert "NativeImage::readBmpLayout(file.data, file.size, layout)" in source
assert source.count("NativeImage::readBmpLayout(file.data, file.size, layout)") == 2
assert "((info.width * bpp + 31u) / 32u) * 4u" not in source
assert "static_cast<uint64_t>(width) * bitsPerPixel" in layout
assert "std::numeric_limits<uint64_t>::max() / height" in layout
assert "pixelBytes > static_cast<uint64_t>(size) - dataOffset" in layout
print("BMP probe/decode checked-layout contract passed")
