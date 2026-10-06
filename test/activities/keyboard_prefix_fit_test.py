#!/usr/bin/env python3
"""Keyboard layout regression using complete production render/touch-height bodies.

Real Ubuntu12/EpdFont/EpdFontFamily/Utf8 metrics are linked. Only display, theme,
controller and scheduling edges are fixtures. --baseline-ref emits the same draw
records from the unoptimized source; --enforce-cost must reject that baseline.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline-ref')
parser.add_argument('--enforce-cost', action='store_true')
parser.add_argument('--compile-only', action='store_true')
parser.add_argument('--output', type=Path)
parser.add_argument('--compare', type=Path)
args = parser.parse_args()


def source(path):
    if args.baseline_ref:
        return subprocess.check_output(['git', '-C', str(ROOT), 'show', args.baseline_ref + ':' + path], text=True)
    return (ROOT / path).read_text()


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if not depth:
                return text[start:i + 1]
    raise AssertionError('Unterminated production function: ' + signature)


def instrument(text, signature, statement):
    start = text.index(signature)
    opening = text.index('{', start) + 1
    return text[:opening] + '\n' + statement + '\n' + text[opening:]


with tempfile.TemporaryDirectory(prefix='keyboard-prefix-') as temp:
    d = Path(temp)
    fixture = ROOT / 'test/activities/keyboard_prefix_fit_fixture.h'
    (d / 'GfxRenderer.h').write_text(fixture.read_text())
    actual = source('lib/GfxRenderer/GfxRenderer.cpp')
    methods = '\n'.join(function(actual, sig) for sig in (
        'uint8_t resolveSdCardStyle(', 'uint16_t sdCardAdvance(',
        'int GfxRenderer::getTextAdvanceX(', 'int GfxRenderer::getTextWidth(',
        'int GfxRenderer::getLineHeight('))
    methods = instrument(methods, 'int GfxRenderer::getTextAdvanceX(',
                         '++cost.measures; cost.measuredBytes += std::strlen(text);')
    if not args.baseline_ref:
        helper = function(actual, 'bool GfxRenderer::getTextFittingPrefix(')
        helper = instrument(helper, 'bool GfxRenderer::getTextFittingPrefix(', '++cost.prefixes;')
        methods += '\n' + helper
    (d / 'measurement.cpp').write_text('#include <GfxRenderer.h>\n#include <Utf8.h>\n#include <Logging.h>\n'
        '#define millis testMillis\n#define vTaskDelay testDelay\n' + methods)
    font = source('lib/EpdFont/EpdFont.cpp')
    for method, counter in [('const EpdGlyph* EpdFont::getGlyph(', 'glyphs'),
                            ('int8_t EpdFont::getKerning(', 'kerns'),
                            ('uint32_t EpdFont::getLigature(', 'ligatures')]:
        font = instrument(font, method, '++cost.' + counter + ';')
    (d / 'EpdFont.cpp').write_text('#include <GfxRenderer.h>\n' + font)
    for stem, path in [('EpdFontFamily', 'lib/EpdFont/EpdFontFamily.cpp'),
                       ('Utf8', 'lib/Utf8/Utf8.cpp'), ('SdCardFont', 'lib/EpdFont/SdCardFont.cpp')]:
        (d / (stem + '.cpp')).write_text(source(path))
    theme = source('src/components/themes/BaseTheme.h')
    (d / 'theme_metrics.h').write_text(theme[theme.index('struct ThemeMetrics {'):theme.index('\nclass BaseTheme')])
    keyboard_header = source('src/activities/util/KeyboardEntryActivity.h')
    keyboard_header = re.sub(r'^#include.*\n', '', keyboard_header, flags=re.M)
    (d / 'keyboard_class.h').write_text(keyboard_header)
    keyboard = source('src/activities/util/KeyboardEntryActivity.cpp')
    signatures = ['KeyboardEntryActivity::~KeyboardEntryActivity()',
                  'KeyboardEntryActivity::RenderState KeyboardEntryActivity::captureRenderState()',
                  'int KeyboardEntryActivity::measureInputHeightForTouch(',
                  'void KeyboardEntryActivity::render(']
    (d / 'keyboard_methods.inc').write_text('\n'.join(function(keyboard, sig) for sig in signatures))
    (d / 'translations.inc').write_text('\n'.join('#define ' + name + ' "' + name + '"'
        for name in sorted(set(re.findall(r'\bSTR_[A-Z_]+\b', keyboard)))))
    files = [ROOT / 'test/activities/keyboard_prefix_fit_fixture.cpp', d / 'measurement.cpp',
             d / 'EpdFont.cpp', d / 'EpdFontFamily.cpp', d / 'SdCardFont.cpp', d / 'Utf8.cpp']
    cmd = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare']
    if os.environ.get('PREFIX_SANITIZE') == '1':
        cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.baseline_ref:
        cmd += ['-DBASELINE']
    if args.enforce_cost:
        cmd += ['-DENFORCE_COST']
    cmd += ['-I' + str(p) for p in [d, ROOT / 'test/fontawesome/stubs', ROOT / 'lib/EpdFont', ROOT / 'lib/Utf8', ROOT / 'src']]
    if args.compile_only:
        for i, file in enumerate(files):
            subprocess.run(cmd + ['-c', str(file), '-o', str(d / f'unit-{i}.o')], check=True)
        print('Compiled complete keyboard render/height and production font/renderer translation units')
    else:
        subprocess.run(cmd + [str(p) for p in files] + ['-o', str(d / 'test')], check=True)
        result = subprocess.run([str(d / 'test')], capture_output=True, timeout=240)
        print(result.stderr.decode(), end='')
        result.check_returncode()
        if args.output:
            args.output.write_bytes(result.stdout)
        if args.compare:
            assert result.stdout == args.compare.read_bytes(), 'Keyboard draw/touch-height records changed'
        # Baseline b91bc323fb736eb6a13ea1a9403470cf699c6128.
        # Complete baseline records include input lines, cursor, field, password,
        # keyboard keys, hints, repeat/mutation and screen/theme variants.
        expected = '1fb62b8d9171efe9ce4d78984cb0b15ffa2e992e5dba65e30be7a12610f91bb2'
        digest = hashlib.sha256(result.stdout).hexdigest()
        assert digest == expected, 'Keyboard baseline layout snapshot digest changed: ' + digest
        print('Keyboard prefix regression passed; draw-record SHA256=' + digest)
