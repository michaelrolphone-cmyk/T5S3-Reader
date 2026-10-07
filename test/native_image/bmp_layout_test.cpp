#include "BmpLayout.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
void put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8u * i));
}

std::vector<uint8_t> makeBmp(int32_t width, int32_t height, uint16_t bpp,
                             uint32_t dataOffset, size_t size) {
  std::vector<uint8_t> bytes(size, 0);
  if (size < 54) return bytes;
  bytes[0] = 'B';
  bytes[1] = 'M';
  put32(bytes, 2, static_cast<uint32_t>(size));
  put32(bytes, 10, dataOffset);
  put32(bytes, 14, 40);
  put32(bytes, 18, static_cast<uint32_t>(width));
  put32(bytes, 22, static_cast<uint32_t>(height));
  put16(bytes, 26, 1);
  put16(bytes, 28, bpp);
  put32(bytes, 30, 0);
  return bytes;
}

bool accepts(const std::vector<uint8_t>& bytes, NativeImage::BmpLayout* out = nullptr) {
  NativeImage::BmpLayout layout;
  const bool valid = NativeImage::readBmpLayout(bytes.data(), bytes.size(), layout);
  if (valid && out) *out = layout;
  return valid;
}
}  // namespace

int main() {
  // This is the reported 32-bit overflow: old row arithmetic collapses a
  // 2 GiB row to four bytes, allowing the 58-byte file through its extent test.
  constexpr uint32_t kOverflowWidth = 0x20000001u;
  const uint32_t oldRowBytes = ((kOverflowWidth * 32u + 31u) / 32u) * 4u;
  assert(oldRowBytes == 4u);
  assert(!accepts(makeBmp(static_cast<int32_t>(kOverflowWidth), 1, 32, 54, 58)));

  NativeImage::BmpLayout layout;
  assert(accepts(makeBmp(1, 1, 24, 54, 58), &layout));
  assert(layout.width == 1 && layout.height == 1 && layout.rowBytes == 4);
  assert(layout.dataOffset == 54 && layout.bitsPerPixel == 24 && !layout.topDown);

  // Top-down rows and valid palette boundary remain accepted.
  assert(accepts(makeBmp(1, -1, 32, 54, 58), &layout));
  assert(layout.topDown && layout.rowBytes == 4);
  assert(accepts(makeBmp(1, 1, 1, 62, 66), &layout));
  assert(layout.paletteOffset == 54 && layout.rowBytes == 4);

  // Reject unrepresentable height, truncated multi-row data, a missing
  // indexed palette, and a DIB header extending past the input.
  assert(!accepts(makeBmp(1, std::numeric_limits<int32_t>::min(), 24, 54, 58)));
  assert(!accepts(makeBmp(1, 2, 24, 54, 58)));
  assert(!accepts(makeBmp(1, 1, 8, 54, 58)));
  auto badDib = makeBmp(1, 1, 24, 54, 58);
  put32(badDib, 14, 0xfffffff0u);
  assert(!accepts(badDib));

  return 0;
}
