#include "runtime/resources/RadioPower.h"
#include <cassert>
#include <vector>
std::vector<bool> writes;
bool working = true;
namespace BoardT5S3 {
bool writePca9535Pin(int, bool on) { writes.push_back(on); return working; }
bool setPca9535PinMode(int, int) { return working; }
}
int main() {
  using namespace RadioPower;
  working = false;
  assert(!acquire(Owner::Gps));
  working = true; writes.clear();
  assert(acquire(Owner::Gps));
  assert(acquire(Owner::Gps));
  assert(acquire(Owner::LoRa));
  release(Owner::Gps);
  assert((writes == std::vector<bool>{true}));
  release(Owner::Gps);
  assert((writes == std::vector<bool>{true}));
  release(Owner::LoRa);
  assert((writes == std::vector<bool>{true, false}));
  writes.clear();
  assert(acquire(Owner::LoRa));
  assert(acquire(Owner::Gps));
  release(Owner::LoRa);
  assert((writes == std::vector<bool>{true}));
  release(Owner::Gps);
  assert((writes == std::vector<bool>{true, false}));
}
