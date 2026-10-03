#!/usr/bin/env python3
"""Compile production link handling/cache methods with Expat and host I/O fixtures."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(os.environ.get('EPUB_SOURCE_ROOT', Path(__file__).resolve().parents[2]))


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    pos = opening + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


parser = (ROOT / 'lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp').read_text()
section = (ROOT / 'lib/Epub/Epub/Section.cpp').read_text()
helpers = '\n'.join(function(parser, sig) for sig in
                    ('bool isWhitespace(', 'const char* getAttribute(', 'bool isInternalEpubLink('))
anchor = parser[parser.index('  if (strcmp(name, "a") == 0) {', parser.index('// Detect internal')):
                parser.index('  const float emSize', parser.index('// Detect internal'))]
close = parser[parser.index('  // Closing a footnote link'):parser.index('  // Leaving skip')]
cache = '\n'.join(function(section, sig) for sig in
                  ('void Section::writeSectionFileHeader(', 'bool Section::loadSectionFile(',
                   'bool Section::clearCache('))
constants = section[section.index('constexpr uint8_t SECTION_FILE_VERSION'):
                    section.index('struct PageLutEntry')]

harness = r'''
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <expat.h>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
void require(bool value, const char* what) {
  if (!value) { fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
''' + helpers + r'''
struct FootnoteEntry { char number[32]{}; char href[256]{}; };
struct StyleStackEntry { int depth=0; bool hasUnderline=false; bool underline=false; };
struct TextBlock { size_t size() const { return 1; } };
struct LinkParser {
  int partWordBufferIndex=0, depth=0, footnoteLinkDepth=0, currentFootnoteLinkTextLen=0;
  int underlineUntilDepth=INT_MAX, wordsExtractedInBlock=0, flushes=0, styleUpdates=0;
  bool nextWordContinues=false, insideFootnoteLink=false;
  FootnoteEntry currentFootnote;
  TextBlock block;
  TextBlock* currentTextBlock=&block;
  std::vector<StyleStackEntry> inlineStyleStack;
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;
  void flushPartWordBuffer() { ++flushes; partWordBufferIndex=0; }
  void updateEffectiveInlineStyle() { ++styleUpdates; }
  static void start(void* data, const XML_Char* name, const XML_Char** atts) {
    auto* self=static_cast<LinkParser*>(data);
''' + anchor + r'''
    ++self->depth;
  }
  static void end(void* data, const XML_Char*) {
    auto* self=static_cast<LinkParser*>(data);
    --self->depth;
''' + close + r'''
  }
  static void characters(void* data, const XML_Char* text, int length) {
    auto* self=static_cast<LinkParser*>(data);
    if (!self->insideFootnoteLink) return;
    size_t count=std::min(static_cast<size_t>(length), sizeof(self->currentFootnote.number)-1);
    memcpy(self->currentFootnote.number, text, count);
    self->currentFootnote.number[count]='\0';
  }
};
bool parse(const std::string& xml, size_t chunk, LinkParser& state) {
  XML_Parser p=XML_ParserCreate(nullptr);
  require(p!=nullptr, "Expat allocation");
  XML_SetUserData(p, &state);
  XML_SetElementHandler(p, LinkParser::start, LinkParser::end);
  XML_SetCharacterDataHandler(p, LinkParser::characters);
  bool ok=true;
  for (size_t offset=0; offset<xml.size(); offset+=chunk) {
    size_t n=std::min(chunk, xml.size()-offset);
    if (XML_Parse(p, xml.data()+offset, static_cast<int>(n), offset+n==xml.size())==XML_STATUS_ERROR) {
      ok=false; break;
    }
  }
  XML_ParserFree(p);
  return ok;
}
struct File {
  std::vector<uint8_t> bytes; size_t pos=0; bool open=false; int closes=0;
  explicit operator bool() const { return open; }
  void close() { require(open, "close only open cache"); open=false; ++closes; }
};
namespace serialization {
  template<class T> void writePod(File& file, const T& value) {
    auto* p=reinterpret_cast<const uint8_t*>(&value);
    file.bytes.insert(file.bytes.end(), p, p+sizeof(T));
  }
  template<class T> void readPod(File& file, T& value) {
    require(file.pos+sizeof(T)<=file.bytes.size(), "cache fixture read bounds");
    memcpy(&value, file.bytes.data()+file.pos, sizeof(T)); file.pos+=sizeof(T);
  }
}
struct StorageFixture {
  bool failOpen=false, failRemove=false, present=true; int removes=0;
  bool openFileForRead(const char*, const std::string&, File& file) {
    if (failOpen || !present) return false;
    file.open=true; file.pos=0; return true;
  }
  bool exists(const char*) const { return present; }
  bool remove(const char*) { ++removes; if (failRemove) return false; present=false; return true; }
} Storage;
struct Section {
  File file; std::string filePath="/cache/sections/0.bin"; uint16_t pageCount=7;
  void writeSectionFileHeader(int, float, bool, uint8_t, uint16_t, uint16_t, bool, bool, uint8_t);
  bool loadSectionFile(int, float, bool, uint8_t, uint16_t, uint16_t, bool, bool, uint8_t);
  bool clearCache() const;
};
''' + constants + cache + r'''
int main() {
  // First assertion is the original canonical #240 trigger.
  LinkParser originalTrigger;
  require(parse("<p><a href=\"HTTP://example.com/\">1</a></p>", 512, originalTrigger), "baseline XHTML parse");
  require(originalTrigger.pendingFootnotes.empty(), "mixed-case external URI became an internal footnote");
  const char* external[]={"http://example.com", "hTtPs://example.com", "MAILTO:a@example.com",
    "FTP://example.com/a", "TEL:123", "JAVASCRIPT:void(0)", "urn:isbn:123", "data:text/plain,hello",
    "custom+name.2-x:value", "a:", "//example.com/a", " \tHTTPS://example.com\r\n", "\nurn:test"};
  const char* internal[]={"#note", "chapter.xhtml#note", "../Text/chapter.xhtml", "./urn:chapter.xhtml",
    "/OPS/chapter.xhtml", "Text/file:name.xhtml", "chapter.xhtml?next=https://example.com",
    "#https://example.com", "caf\xc3\xa9.xhtml", "1chapter.xhtml", "a%3Ab.xhtml"};
  require(!isInternalEpubLink(nullptr) && !isInternalEpubLink("") && !isInternalEpubLink(" \r\n\t"),
          "missing or blank href");
  for (auto href: external) require(!isInternalEpubLink(href), href);
  for (auto href: internal) require(isInternalEpubLink(href), href);
  int cases=0;
  for (size_t chunk: {size_t(1),size_t(7),size_t(512),size_t(1024)}) {
    for (auto href: external) {
      LinkParser state; state.partWordBufferIndex=3;
      require(parse(std::string("<p><a href=\"")+href+"\">1</a></p>", chunk, state), "external XHTML parse");
      require(state.pendingFootnotes.empty() && state.inlineStyleStack.empty() && state.flushes==0,
              "external anchor changed internal-footnote state or styling");
      ++cases;
    }
    for (auto href: internal) {
      LinkParser state; state.partWordBufferIndex=3;
      require(parse(std::string("<p><a href=\"")+href+"\">1</a></p>", chunk, state), "internal XHTML parse");
      require(state.pendingFootnotes.size()==1 && std::string(state.pendingFootnotes[0].second.href)==href,
              "internal anchor target not preserved");
      require(state.flushes==1 && state.styleUpdates==1 && !state.insideFootnoteLink,
              "internal anchor styling/close");
      ++cases;
    }
    LinkParser missing;
    require(parse("<p><a>1</a><a href=\"\">2</a></p>", chunk, missing) && missing.pendingFootnotes.empty(),
            "missing href creates no footnote");
    LinkParser mixed;
    require(parse("<p><a href=\"URN:test\">1</a><a href=\"#note\">2</a><a href=\"HTTP://x\">3</a></p>",
                  chunk, mixed) && mixed.pendingFootnotes.size()==1 && !mixed.insideFootnoteLink,
            "external links do not contaminate a later internal link");
    LinkParser malformed;
    require(!parse("<p><a href=\"HTTP://x\">1</p>", chunk, malformed), "malformed XML fails");
    LinkParser retry;
    require(parse("<p><a href=\"#retry\">4</a></p>", chunk, retry) && retry.pendingFootnotes.size()==1,
            "fresh parse retries after malformed input");
  }
  // Old footnote metadata must be rejected before reading the rest of a cache.
  Section section;
  section.file.bytes={23};
  require(!section.loadSectionFile(1, 1.0f, false, 0, 540, 960, false, true, 0), "reject pre-fix cache");
  require(!section.file.open && section.file.closes==1 && !Storage.present, "stale cache closed and removed");
  Storage.present=true; Storage.failRemove=true;
  require(!section.loadSectionFile(1, 1.0f, false, 0, 540, 960, false, true, 0), "cleanup failure cannot accept stale cache");
  require(!section.file.open, "cleanup failure closes file");
  Storage.failRemove=false;
  require(!section.loadSectionFile(1, 1.0f, false, 0, 540, 960, false, true, 0), "retry stale removal");
  Storage.present=true; section.file.bytes.clear(); section.file.open=true;
  section.writeSectionFileHeader(1, 1.0f, false, 0, 540, 960, false, true, 0);
  section.file.close();
  require(section.file.bytes[0]>23, "new cache version");
  Storage.failOpen=true;
  require(!section.loadSectionFile(1, 1.0f, false, 0, 540, 960, false, true, 0), "read-open failure");
  Storage.failOpen=false; section.pageCount=0;
  require(section.loadSectionFile(1, 1.0f, false, 0, 540, 960, false, true, 0) && section.pageCount==7 && !section.file.open,
          "new cache retry and round trip");
  require(!section.loadSectionFile(2, 1.0f, false, 0, 540, 960, false, true, 0) && !section.file.open,
          "other cache parameter mismatches still rejected");
  printf("PASS: %d production anchor cases, mixed/error/retry and section cache cleanup/roundtrip\n", cases);
}
'''
with tempfile.TemporaryDirectory(prefix='epub-links-') as directory:
    tmp = Path(directory)
    source = tmp / 'test.cpp'
    source.write_text(harness)
    flags = ['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.environ.get('EPUB_SANITIZE') == '1' else []
    objects = []
    for name in ('xmlparse', 'xmlrole', 'xmltok'):
        obj = tmp / (name + '.o')
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-DXML_GE=0', '-DXML_CONTEXT_BYTES=1024', *flags,
                       '-I'+str(ROOT/'lib/expat'), '-c', str(ROOT/'lib/expat'/(name+'.c')), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = tmp / 'test'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                   '-I'+str(ROOT/'lib/expat'), str(source), *objects, '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
