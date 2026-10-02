#!/usr/bin/env python3
"""Exercise the real detached constructor and glyph-resolution implementation.

Only physical display/storage and bitmap-cache backends are stubbed. This
catches a detached target losing compressed glyphs without an ESP32 build.
"""
import os
import pathlib
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
source = (repo / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
begin = source.index('const uint8_t* GfxRenderer::getGlyphBitmap(')
end = source.index('void GfxRenderer::ensureSdCardFontReady(', begin)
resolve = source[begin:end]
begin = source.index('void GfxRenderer::freeBwBufferChunks()')
end = source.index('/**', begin)
cleanup = source[begin:end]
begin = source.index("int GfxRenderer::getScreenWidth() const")
end = source.index("int GfxRenderer::getSpaceWidth(", begin)
cleanup += source[begin:end]
with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary)
    (root / 'HalStorage.h').write_text('#pragma once\nclass FsFile {};\n')
    (root / 'test.cpp').write_text('''#include <cassert>
#include <cstdlib>
#include <GfxRenderer.h>
class TestSurface final : public DisplaySurface {
public:
 bool isReady() const override { assert(false); return false; }
 DisplaySurfaceInfo getSurfaceInfo() const override { assert(false); return {}; }
 uint8_t* getFrameBuffer() const override { assert(false); return nullptr; }
 void clearScreen(uint8_t) const override { assert(false); }
 void drawImage(const uint8_t*,uint16_t,uint16_t,uint16_t,uint16_t,bool) const override { assert(false); }
 void drawImageTransparent(const uint8_t*,uint16_t,uint16_t,uint16_t,uint16_t,bool) const override { assert(false); }
 void displayBuffer(DisplayPresentMode,bool) override { assert(false); }
 void requestNextRefresh(DisplayPresentMode) override { assert(false); }
 void requestNextDisplayEffect(DisplayEffect) override { assert(false); }
 void copyGrayscaleLsbBuffers(const uint8_t*) override { assert(false); }
 void copyGrayscaleMsbBuffers(const uint8_t*) override { assert(false); }
 bool captureGrayscaleBaseBuffer(const uint8_t*) override { assert(false); return false; }
 bool grayscaleBuffersReady() const override { assert(false); return false; }
 void cleanupGrayscaleBuffers(const uint8_t*) override { assert(false); }
 void displayGrayBuffer(DisplayPresentMode) override { assert(false); }
};
#define LOG_ERR(...) ((void)0)
static unsigned decompressions=0;
static const uint8_t ink[]={0x81,0x42};
class FontDecompressor {
public:
 const uint8_t* getBitmap(const EpdFontData*,const EpdGlyph*,uint32_t index) {
   assert(index==0); ++decompressions; return ink;
 }
};
class FontCacheManager {
public:
 FontDecompressor decoder;
 FontDecompressor* getDecompressor() { return &decoder; }
};
class SdCardFont {
public:
 static SdCardFont* fromMissCtx(void* ctx) { return static_cast<SdCardFont*>(ctx); }
 bool isOverflowGlyph(const EpdGlyph*) { return true; }
 const uint8_t* getOverflowBitmap(const EpdGlyph*) { return ink; }
};
''' + resolve + cleanup + '''
int main() {
 TestSurface display;
 GfxRenderer host(display);
 FontCacheManager cache;
 host.setFontCacheManager(&cache);
 uint8_t target[64800]={};
 GfxRenderer page(host,target,960,540);
 assert(page.isInitialized());
 GfxRenderer invalid(host,target,959,540);
 assert(!invalid.isInitialized());
 assert(page.getScreenWidth()==960 && page.getScreenHeight()==540);
 assert(page.getFontCacheManager()==nullptr); // Never inherit scan-only mode.
 EpdGlyph glyph{};
 EpdFontGroup group{};
 EpdFontData compressed{}; compressed.groups=&group; compressed.glyph=&glyph;
 assert(host.getGlyphBitmap(&compressed,&glyph)==ink);
 assert(page.getGlyphBitmap(&compressed,&glyph)==ink);
 assert(decompressions==2);
 // Uncompressed and on-demand SD glyphs retain their source resolution.
 EpdFontData plain{}; plain.bitmap=ink; glyph.dataOffset=1;
 assert(page.getGlyphBitmap(&plain,&glyph)==ink+1);
 SdCardFont sd; plain.glyphMissCtx=&sd;
 assert(page.getGlyphBitmap(&plain,&glyph)==ink);
 host.setFontCacheManager(nullptr);
 assert(page.getGlyphBitmap(&compressed,&glyph)==nullptr);
 for(auto byte:target) assert(byte==0); // Resolving glyphs never paints the panel.
}
''')
    binary = root / 'test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I'+str(root),
                    '-I'+str(repo/'lib/GfxRenderer'), '-I'+str(repo/'lib/DisplaySurface'), '-I'+str(repo/'lib/EpdFont'),
                    str(root/'test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0'}, check=True)
print('Detached reader: compressed, plain and SD glyph resolution; no scan-mode inheritance PASS')
