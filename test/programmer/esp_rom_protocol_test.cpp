#include "runtime/programmer/EspRomProtocol.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

int main() {
  using namespace EspRomProtocol;
  uint8_t raw[kCommandBytes]{}, framed[kFramedBytes]{};
  const uint8_t payload[] = {0xc0, 0xdb, 0x55};
  auto size = encode(0x08, payload, sizeof(payload), checksum(payload, sizeof(payload)),
                     raw, sizeof(raw), framed, sizeof(framed));
  assert(size == 15); // Header 8 + payload 3 + escapes 2 + delimiters 2
  assert(framed[0] == 0xc0 && framed[size - 1] == 0xc0);
  assert(raw[0] == 0 && raw[1] == 0x08 && raw[2] == 3 && raw[3] == 0);
  assert(raw[8] == 0xc0 && raw[9] == 0xdb && raw[10] == 0x55);
  assert(!encode(0x08, payload, sizeof(payload), 0, raw, 9, framed, sizeof(framed)));
  assert(!encode(0x08, nullptr, 1, 0, raw, sizeof(raw), framed, sizeof(framed)));
  assert(!encode(0x08, payload, sizeof(payload), 0, raw, sizeof(raw), framed, 8));
  Decoder packet;
  for (size_t i = 0; i < size; ++i) {
    auto result = packet.feed(framed[i]);
    assert(result == (i == size - 1 ? Decoder::Result::Frame : Decoder::Result::More));
  }
  assert(packet.size() == 11 && std::memcmp(packet.data(), raw, 11) == 0);
  Decoder malformed;
  assert(malformed.feed(0xc0) == Decoder::Result::More);
  assert(malformed.feed(0xdb) == Decoder::Result::More);
  assert(malformed.feed(0x34) == Decoder::Result::Invalid);
  uint8_t reply[] = {1, 0x03, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t status = 7, error = 7;
  assert(success(0x03, reply, sizeof(reply), &status, &error) && status == 0 && error == 0);
  assert(!success(0x02, reply, sizeof(reply), &status, &error));
  assert(status == 0xff && error == 0xff);
  reply[8] = 2;
  assert(!success(0x03, reply, sizeof(reply), &status, &error) && status == 2);
  reply[2] = 0xff; reply[3] = 0x7f;
  assert(!success(0x03, reply, sizeof(reply), &status, &error));
  assert(checksum(payload, 0) == 0xef);
  std::cout << "ESP ROM protocol codec tests passed\n";
}
