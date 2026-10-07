#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace NativeImage {

struct BmpLayout {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t rowBytes = 0;
  uint32_t dataOffset = 0;
  uint32_t paletteOffset = 0;
  uint16_t bitsPerPixel = 0;
  bool topDown = false;
};

inline uint16_t bmpLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t bmpLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t bmpSignedLe32(const uint8_t* p) {
  return static_cast<int32_t>(bmpLe32(p));
}

// Validate all offsets and row extents before either probing or decoding a BMP.
inline bool readBmpLayout(const uint8_t* data, size_t size, BmpLayout& out) {
  if (!data || size < 54 || data[0] != 'B' || data[1] != 'M') return false;

  const uint32_t dibBytes = bmpLe32(data + 14);
  const uint64_t dibEnd = 14u + static_cast<uint64_t>(dibBytes);
  if (dibBytes < 40 || dibEnd > size) return false;

  const int32_t signedWidth = bmpSignedLe32(data + 18);
  const int32_t signedHeight = bmpSignedLe32(data + 22);
  if (signedWidth <= 0 || signedHeight == 0) return false;

  const uint32_t width = static_cast<uint32_t>(signedWidth);
  const uint32_t height = static_cast<uint32_t>(signedHeight < 0
                                                    ? -static_cast<int64_t>(signedHeight)
                                                    : signedHeight);
  const uint16_t planes = bmpLe16(data + 26);
  const uint16_t bitsPerPixel = bmpLe16(data + 28);
  const uint32_t compression = bmpLe32(data + 30);
  if (planes != 1 || compression != 0 ||
      (bitsPerPixel != 1 && bitsPerPixel != 4 && bitsPerPixel != 8 && bitsPerPixel != 16 &&
       bitsPerPixel != 24 && bitsPerPixel != 32)) {
    return false;
  }

  const uint32_t dataOffset = bmpLe32(data + 10);
  if (dataOffset < dibEnd || dataOffset > size) return false;

  const uint64_t rowBits = static_cast<uint64_t>(width) * bitsPerPixel;
  const uint64_t rowBytes64 = ((rowBits + 31u) / 32u) * 4u;
  if (rowBytes64 == 0 || rowBytes64 > std::numeric_limits<uint32_t>::max()) return false;
  if (rowBytes64 > std::numeric_limits<uint64_t>::max() / height) return false;
  const uint64_t pixelBytes = rowBytes64 * height;
  if (pixelBytes > static_cast<uint64_t>(size) - dataOffset) return false;

  const uint32_t paletteEntries = bitsPerPixel <= 8 ? (1u << bitsPerPixel) : 0u;
  const uint64_t paletteEnd = dibEnd + static_cast<uint64_t>(paletteEntries) * 4u;
  if (paletteEnd > dataOffset) return false;

  out.width = width;
  out.height = height;
  out.rowBytes = static_cast<uint32_t>(rowBytes64);
  out.dataOffset = dataOffset;
  out.paletteOffset = static_cast<uint32_t>(dibEnd);
  out.bitsPerPixel = bitsPerPixel;
  out.topDown = signedHeight < 0;
  return true;
}

}  // namespace NativeImage
