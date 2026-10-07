#!/usr/bin/env python3
"""Complete production Serial Monitor/native text view and real Ubuntu10 metrics.

Streams/events, display/chrome and clock are fixtures. Counts are host allocation
requests, not live memory or target latency. Compare complete draw records,
result/hit geometry and application cleanup against the source baseline.
"""
from pathlib import Path
import argparse
import functools
import hashlib
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser()
p.add_argument('--baseline-ref')
p.add_argument('--sanitize', action='store_true')
p.add_argument('--enforce-cost', action='store_true')
p.add_argument('--output', type=Path)
p.add_argument('--compare', type=Path)
args = p.parse_args()

@functools.lru_cache(None)
def source(path):
    if args.baseline_ref:
        return subprocess.check_output(['git', '-C', str(ROOT), 'show', args.baseline_ref + ':' + path], text=True)
    return (ROOT / path).read_text()

def function(path, signature):
    text = source(path)
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for i in range(opening, len(text)):
        depth += (text[i] == '{') - (text[i] == '}')
        if not depth:
            return text[start:i + 1]
    raise AssertionError('Unterminated function ' + signature)

with tempfile.TemporaryDirectory(prefix='native-text-app-') as temp:
    build = Path(temp)
    ui = source('src/native/NativeUiBridge.cpp')
    optimized = 'textViewCache.matches' in ui
    parts = [(HERE / 'fixture.h').read_text()]
    if optimized:
        parts += [function('src/native/NativeUiBridge.cpp', 'NativeTextLayoutCache::Key textLayoutKey('),
                  function('src/native/NativeUiBridge.cpp', 'struct TextLayoutCopyBudget') + ';']
    a = ui.index('struct NativeUiLayout {')
    b = ui.index('\nvoid drawChrome', a)
    parts.append(ui[a:b])
    body = function('lib/GfxRenderer/GfxRenderer.cpp', 'int GfxRenderer::getTextWidth(')
    body = body.replace(' const {', ' const { ++counts.measures; counts.measuredBytes+=strlen(text);', 1)
    parts.append(body)
    gfx = source('lib/GfxRenderer/GfxRenderer.cpp')
    if 'bool GfxRenderer::getTruncationPrefix(' in gfx:
        parts.append(function('lib/GfxRenderer/GfxRenderer.cpp', 'bool GfxRenderer::getTruncationPrefix('))
    else:
        parts.append('bool GfxRenderer::getTruncationPrefix(int,const std::string&,int,size_t&,EpdFontFamily::Style)const{return false;}')
    for sig in ['uint8_t styleMaskForStyle(', 'void ensureRoleTextReady(', 'int measureRoleTextWidth(',
                'std::string truncatedPreparedText(', 'int BaseTheme::resolveTextFontId(',
                'int BaseTheme::getLineHeightForRole(', 'std::vector<std::string> BaseTheme::wrappedTextForRole(',
                'void BaseTheme::drawTextForRole(']:
        body = function('src/components/themes/BaseTheme.cpp', sig)
        if sig.startswith('std::vector'):
            body = body.replace('{', '{ ++counts.paragraphs;', 1)
        parts.append(body)
    parts.append(function('src/native/NativeUiBridge.cpp', 'void renderTextView('))
    parts.append(function('src/native/NativeUiBridge.cpp', 'void nativeUiResetTextLayout(') if optimized
                 else 'void nativeUiResetTextLayout(){textViewCache.clear();}')
    parts.append(r'''
extern "C" void setup_metrics(){
 static EpdFont font(&ubuntu_10_regular);
 globalRenderer.width=480;globalRenderer.height=800;
 globalRenderer.fontMap.emplace(UI_10_FONT_ID,EpdFontFamily(&font));
 globalRenderer.fontMap.emplace(SMALL_FONT_ID,EpdFontFamily(&font));
 nativeUiResetTextLayout();
}
extern "C" void finish_metrics(){nativeUiResetTextLayout();assert(textViewCache.lineCount()==0);}
extern "C" void measured_text_view(const t5_ui_chrome_t*c,const char*s,int32_t scroll,t5_ui_text_view_result_t*r,bool emit){
 counts={};countAllocation=true;renderTextView(c,s,scroll,r);countAllocation=false;
 if(emit){
   std::printf("APP bytes=%zu scroll=%d paragraphs=%zu measures=%zu measured_bytes=%zu allocations=%zu allocated_bytes=%zu drawn_lines=%zu total=%u visible=%u max_scroll=%d header=%d row_top=%d row_height=%d page_start=%d page_items=%d row_count=%d hash=%016llx\n",
    strlen(s),scroll,counts.paragraphs,counts.measures,counts.measuredBytes,counts.allocations,counts.allocatedBytes,counts.drawnLines,
    r->total_lines,r->visible_lines,r->max_scroll_lines,hitLayout.headerBottom,hitLayout.rowTop,hitLayout.rowHeight,
    hitLayout.pageStart,hitLayout.pageItems,hitLayout.rowCount,(unsigned long long)drawHash);
   std::fwrite(frameRecord,1,frameUsed,stdout);
 }
}
''')
    (build / 'render.cpp').write_text('\n'.join(parts))
    for path in ['Apps/serial_monitor.c', 'Apps/serial_monitor_implementation.inc',
                 'lib/EpdFont/EpdFont.cpp', 'lib/EpdFont/EpdFontFamily.cpp', 'lib/Utf8/Utf8.cpp']:
        (build / Path(path).name).write_text(source(path))
    flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
    objects = []
    for i, path in enumerate([HERE / 'app_driver.c', build / 'serial_monitor.c']):
        obj = build / f'app{i}.o'
        subprocess.run(['cc', '-std=c11', '-O2', *flags, '-I' + str(ROOT / 'lib/NativeApps/include'),
                        '-I' + str(build), '-I' + str(ROOT / 'Apps'), '-c', str(path), '-o', str(obj)], check=True)
        objects.append(obj)
    cmd = ['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
           '-Wno-mismatched-new-delete', '-Wno-missing-field-initializers', *flags]
    cmd += ['-I' + str(x) for x in [ROOT / 'lib/EpdFont', ROOT / 'lib/Utf8', ROOT / 'src', ROOT / 'lib/NativeApps/include']]
    cmd += [str(x) for x in objects + [build / 'render.cpp', build / 'EpdFont.cpp', build / 'EpdFontFamily.cpp', build / 'Utf8.cpp']]
    subprocess.run(cmd + ['-o', str(build / 'test')], check=True)
    result = subprocess.run([str(build / 'test')], capture_output=True, timeout=120,
                            env=dict(os.environ, ASAN_OPTIONS=os.environ.get('ASAN_OPTIONS', ''),
                                     UBSAN_OPTIONS='halt_on_error=1'))
    print(result.stderr.decode(), end='')
    result.check_returncode()
    output = result.stdout.decode()
    rows = [dict(v.split('=', 1) for v in line.split()[1:]) for line in output.splitlines() if line.startswith('APP bytes=')]
    assert len(rows) == 7
    if args.enforce_cost:
        for row in rows[1:]:
            assert all(int(row[k]) == 0 for k in ['paragraphs', 'measures', 'measured_bytes', 'allocations', 'allocated_bytes']), row
    costs = {'paragraphs', 'measures', 'measured_bytes', 'allocations', 'allocated_bytes'}
    semantic = []
    for line in output.splitlines():
        if line.startswith('APP bytes='):
            line = 'APP ' + ' '.join(word for word in line.split()[1:] if word.split('=')[0] not in costs)
        semantic.append(line)
    transcript = '\n'.join(semantic) + '\n'
    if args.output:
        args.output.write_text(transcript)
    if args.compare:
        assert transcript == args.compare.read_text(), 'Serial Monitor output or UI/cleanup semantics changed'
    digest = hashlib.sha256(transcript.encode()).hexdigest()
    assert digest == 'dddbc471aceeeeac8b6aa8d227fe22a298c9acce4a3d1633800839215108bb0d', 'Original app transcript changed'
    print(json.dumps(rows, indent=2))
    print('Complete Serial Monitor output SHA256=' + digest)
