#!/usr/bin/env python3
"""Actual production truncators and font APIs; only display/SD readiness/time are fixtures."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline-ref')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--enforce-cost', action='store_true')
parser.add_argument('--compile-only', action='store_true')
args = parser.parse_args()

def source(path, baseline=False):
    if baseline:
        return subprocess.check_output(['git', '-C', str(ROOT), 'show', args.baseline_ref + ':' + path], text=True)
    return (ROOT / path).read_text()

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

with tempfile.TemporaryDirectory(prefix='title-truncation-') as temp:
    out = Path(temp)
    gfx = source('lib/GfxRenderer/GfxRenderer.cpp', bool(args.baseline_ref))
    theme = source('src/components/themes/BaseTheme.cpp', bool(args.baseline_ref))
    header = r'''
#include <EpdFontFamily.h>
#include <Utf8.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/notosans_12_regular.h>
#pragma GCC diagnostic pop
#define LOG_ERR(...) do {} while (0)
uint32_t clockNow=0, clockStep=0;
size_t waits=0, helperSteps=0;
uint32_t millis(){uint32_t n=clockNow;clockNow+=clockStep;return n;}
void vTaskDelay(uint32_t n){assert(n==1);++waits;clockNow+=n;}
struct GfxRenderer {
 std::map<int,EpdFontFamily> fontMap;
 std::set<int> sd;
 mutable size_t calls=0,bytes=0,prepares=0,advanceCalls=0;
 bool isSdCardFont(int id) const {return sd.count(id)!=0;}
 void ensureSdCardFontReady(int,const char*,uint8_t) const {++prepares;}
 int actualWidth(int,const char*,EpdFontFamily::Style) const;
 int getTextWidth(int id,const char* s,EpdFontFamily::Style st) const {++calls;bytes+=strlen(s);return actualWidth(id,s,st);}
 // The SD branch is an unchanged-call-boundary fixture, not an SD timing model.
 int getTextAdvanceX(int id,const char* s,EpdFontFamily::Style st) const {++advanceCalls;return getTextWidth(id,s,st);}
 bool getTruncationPrefix(int,const std::string&,int,size_t&,EpdFontFamily::Style=EpdFontFamily::REGULAR) const;
 std::string truncatedText(int,const char*,int,EpdFontFamily::Style=EpdFontFamily::REGULAR) const;
};
'''
    body = function(gfx, 'int GfxRenderer::getTextWidth(').replace('GfxRenderer::getTextWidth', 'GfxRenderer::actualWidth')
    if args.baseline_ref:
        body += '\nbool GfxRenderer::getTruncationPrefix(int,const std::string&,int,size_t&,EpdFontFamily::Style)const{return false;}\n'
    else:
        helper = function(gfx, 'bool GfxRenderer::getTruncationPrefix(')
        helper = helper.replace('const uint32_t cp = utf8NextCodepoint(&cursor);', '++helperSteps; const uint32_t cp = utf8NextCodepoint(&cursor);')
        body += '\n' + helper
    body += '\n' + function(gfx, 'std::string GfxRenderer::truncatedText(')
    for signature in ('uint8_t styleMaskForStyle(', 'void ensureRoleTextReady(', 'int measureRoleTextWidth(', 'std::string truncatedPreparedText('):
        body += '\n' + function(theme, signature)
    # The reference is the unchanged descending-prefix policy. Each width uses
    # complete real EpdFont::getTextDimensions through the production renderer API.
    reference = r'''
std::string reference(const GfxRenderer& r,int id,const char* text,int maxWidth,EpdFontFamily::Style st,bool role) {
 if (!text || maxWidth<=0) return {};
 std::string item=text; const char* ellipsis="\xe2\x80\xa6";
 if(role){ensureRoleTextReady(r,id,item.c_str(),st);ensureRoleTextReady(r,id,ellipsis,st);}
 const auto width=[&](const char* s){return role?measureRoleTextWidth(r,id,s,st):r.getTextWidth(id,s,st);};
 if(width(item.c_str())<=maxWidth)return item;
 while(!item.empty()&&width((item+ellipsis).c_str())>=maxWidth)utf8RemoveLastChar(item);
 return item.empty()?ellipsis:item+ellipsis;
}
'''
    flags = ['-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror']
    if args.baseline_ref: flags += ['-DBASELINE']
    if args.enforce_cost: flags += ['-DENFORCE_COST']
    if args.sanitize: flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    cpp = out/'test.cpp'
    cpp.write_text(header + body + reference + (ROOT/'test/title_truncation/truncation_test.cpp').read_text())
    binary = out/'test'
    command = [os.environ.get('CXX', 'c++'), *flags, '-I'+str(ROOT/'lib/EpdFont'), '-I'+str(ROOT/'lib/Utf8'), str(cpp)]
    command += [str(ROOT / ('lib/EpdFont/' + name + '.cpp')) for name in ('EpdFont', 'EpdFontFamily')]
    command += [str(ROOT/'lib/Utf8/Utf8.cpp'), '-o', str(binary)]
    if args.compile_only:
        units = [cpp, ROOT/'lib/EpdFont/EpdFont.cpp', ROOT/'lib/EpdFont/EpdFontFamily.cpp', ROOT/'lib/Utf8/Utf8.cpp']
        for index, unit in enumerate(units):
            subprocess.run([os.environ.get('CXX', 'c++'), *flags, '-I'+str(ROOT/'lib/EpdFont'),
                            '-I'+str(ROOT/'lib/Utf8'), '-c', str(unit), '-o', str(out/f'unit-{index}.o')],
                           check=True, timeout=90)
        print('Production truncation fixture and real font/UTF-8 translation units compile: PASS')
    else:
        subprocess.run(command, check=True, timeout=90)
        subprocess.run([str(binary)], check=True, timeout=90)
