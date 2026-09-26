#include "runtime/packages/PackageRteZipInstall.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace RuntimePackages;
namespace {
struct Archive {
  std::vector<uint8_t> bytes;
  bool read(uint64_t offset, uint8_t* dest, size_t length) const {
    if (!dest || offset > bytes.size() || length > bytes.size() - offset) return false;
    if (length) std::memcpy(dest, bytes.data() + offset, length);
    return true;
  }
};
Archive makeArchive() {
  const uint8_t manifest[] = {'{', '}'};
  const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
  RteZipSourceFile files[] = {
      {kOrdinaryManifestName, manifest, sizeof(manifest)},
      {"resource.dat", payload, sizeof(payload)},
  };
  Archive zip;
  const auto write = [&](const uint8_t* data, size_t n) {
    zip.bytes.insert(zip.bytes.end(), data, data + n);
    return true;
  };
  uint32_t written = 0;
  assert(packRteZip(files, 2, write, &written) == RteZipResult::Ready);
  assert(written == zip.bytes.size());
  return zip;
}
RteZipView inspect(const Archive& zip) {
  RteZipView view{};
  auto readAt = [&](uint64_t at, uint8_t* dest, size_t n) {
    return zip.read(at, dest, n);
  };
  assert(inspectRteZip(readAt, zip.bytes.size(), view) == RteZipResult::Ready);
  return view;
}
bool valid(const Archive& zip, const RteZipView& view) {
  auto readAt = [&](uint64_t at, uint8_t* dest, size_t n) {
    return zip.read(at, dest, n);
  };
  return RteZipInstallDetail::validateArchive(readAt, view);
}
} // namespace

int main() {
  Archive zip = makeArchive();
  RteZipView view = inspect(zip);
  assert(valid(zip, view));

  // The old inspector checked only local-vs-central metadata: corrupting a
  // payload passed inspection and could skip the ZIP transport CRC entirely.
  Archive corrupt = zip;
  corrupt.bytes[view.entries[1].dataOffset] ^= 0x80u;
  RteZipView corruptView = inspect(corrupt);
  assert(!valid(corrupt, corruptView));

  // A view with central entries out of local order cannot be a canonical
  // package even though each independent offset still points to valid data.
  RteZipView reversed = view;
  const auto first = reversed.entries[0];
  reversed.entries[0] = reversed.entries[1];
  reversed.entries[1] = first;
  reversed.manifestIndex = 1;
  assert(!valid(zip, reversed));

  // Insert an undeclared gap between payloads and the central directory.
  // Rebase EOCD to make the metadata individually valid: topology must catch
  // the hidden byte before extraction or any managed-directory mutation.
  Archive hidden = zip;
  const uint32_t oldCentral = RteZipDetail::le32(
      hidden.bytes.data() + hidden.bytes.size() - kRteZipEocdBytes + 16);
  hidden.bytes.insert(hidden.bytes.begin() + oldCentral, 0x41);
  RteZipDetail::put32(hidden.bytes.data() + hidden.bytes.size() - kRteZipEocdBytes + 16,
                      oldCentral + 1);
  RteZipView hiddenView = inspect(hidden);
  assert(!valid(hidden, hiddenView));

  // Incomplete storage reads must fail before ordinary staging begins.
  auto truncatedRead = [&](uint64_t offset, uint8_t* dest, size_t n) {
    if (offset == view.entries[1].dataOffset) return false;
    return zip.read(offset, dest, n);
  };
  assert(!RteZipInstallDetail::validateArchive(truncatedRead, view));
  return 0;
}
