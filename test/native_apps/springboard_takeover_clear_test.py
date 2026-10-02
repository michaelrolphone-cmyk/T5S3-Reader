#!/usr/bin/env python3
"""Exercise production RAM clearing with a suspended or detached display.

Compile the exact renderer method bodies, not a second implementation of the
fix. The surface and clock are host doubles; this does not qualify panel I/O.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        if end >= len(source):
            raise ValueError(f"Unclosed method: {signature}")
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main() -> None:
    source = (ROOT / "lib/GfxRenderer/GfxRenderer.cpp").read_text()
    methods = "\n".join(method(source, signature) for signature in (
        "void GfxRenderer::clearScreen(",
        "void GfxRenderer::displayBuffer(",
    ))
    harness = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

enum class DisplayPresentMode { Clean, Quality, Balanced, LowLatency };
static unsigned long start_ms = 0;
static unsigned long millis() { return 100; }
template<typename... Args> void logStub(Args...) {}
#define LOG_ERR(...) logStub(__VA_ARGS__)
#define LOG_DBG(...) logStub(__VA_ARGS__)

struct Surface {
  bool ready = true;
  uint8_t* pixels;
  size_t bytes;
  mutable unsigned clears = 0;
  unsigned presents = 0;
  bool isReady() const { return ready; }
  void clearScreen(uint8_t color) const {
    ++clears;
    std::memset(pixels, color, bytes);
  }
  void displayBuffer(DisplayPresentMode) { ++presents; }
};
struct GfxRenderer {
  Surface& display;
  bool initialized;
  uint8_t* frameBuffer;
  uint32_t frameBufferSize;
  void clearScreen(uint8_t color = 0xff) const;
  void displayBuffer(DisplayPresentMode mode = DisplayPresentMode::Quality) const;
};
''' + methods + r'''
static void all(const std::vector<uint8_t>& bytes, uint8_t expected) {
  assert(std::all_of(bytes.begin(), bytes.end(), [=](uint8_t b) { return b == expected; }));
}
int main() {
  constexpr size_t bytes = 960u * 540u / 8u;
  constexpr size_t guard = 16;
  std::vector<uint8_t> backing(bytes + 2 * guard, 0xa5);
  Surface surface{true, backing.data() + guard, bytes};
  GfxRenderer renderer{surface, false, surface.pixels, static_cast<uint32_t>(bytes)};
  // A failed/unattempted begin must not mutate even an existing buffer.
  renderer.clearScreen();
  renderer.displayBuffer();
  all(backing, 0xa5);
  assert(surface.clears == 0 && surface.presents == 0);
  renderer.initialized = true;
  renderer.clearScreen();
  assert(std::all_of(surface.pixels, surface.pixels + bytes, [](uint8_t b) { return b == 0xff; }));

  // Native UI video retains this RAM allocation while the backend is unready.
  // Rasterize neighboring pages before the current one, as Springboard does.
  surface.ready = false;
  for (unsigned page = 0; page < 3; ++page) {
    renderer.clearScreen();
    assert(std::all_of(surface.pixels, surface.pixels + bytes, [](uint8_t b) { return b == 0xff; }));
    surface.pixels[page * 100 + 7] = 0; // Page-specific label/icon marker.
  }
  assert(surface.pixels[7] == 0xff && surface.pixels[107] == 0xff);
  assert(surface.pixels[207] == 0);
  // Gray-plane replay also needs a fresh all-zero scratch plane each time.
  renderer.clearScreen(0x00);
  assert(std::all_of(surface.pixels, surface.pixels + bytes, [](uint8_t b) { return b == 0; }));
  renderer.displayBuffer();
  assert(surface.presents == 0); // Never reclaim/drive the suspended backend.

  // Detached targets borrow fonts/backend identity, not backend framebuffer RAM.
  std::vector<uint8_t> detached(37 * 24, 0xcc);
  GfxRenderer target{surface, true, detached.data(), static_cast<uint32_t>(detached.size())};
  for (bool ready : {false, true}) {
    surface.ready = ready;
    target.clearScreen(0x55);
    all(detached, 0x55);
    assert(std::all_of(surface.pixels, surface.pixels + bytes, [](uint8_t b) { return b == 0; }));
  }
  assert(surface.clears == 0); // Clearing is strictly local RAM work.
  renderer.displayBuffer();
  assert(surface.presents == 1); // Presentation still works after resume.

  renderer.frameBuffer = nullptr;
  renderer.clearScreen();
  renderer.displayBuffer();
  assert(surface.presents == 1);
  target.frameBufferSize = 0;
  target.clearScreen();
  all(detached, 0x55);
  assert(std::all_of(backing.begin(), backing.begin() + guard, [](uint8_t b) { return b == 0xa5; }));
  assert(std::all_of(backing.end() - guard, backing.end(), [](uint8_t b) { return b == 0xa5; }));
  std::puts("Springboard takeover clearing, gray scratch, detached target and presentation guards: PASS");
}
'''
    with tempfile.TemporaryDirectory(prefix="springboard-clear-") as directory:
        cpp = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        cpp.write_text(harness)
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                       str(cpp), "-o", str(binary)], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
