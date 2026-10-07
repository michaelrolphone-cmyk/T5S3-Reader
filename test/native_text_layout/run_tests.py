#!/usr/bin/env python3
"""Bounded cache and pre-cache/current production frame differential regressions.

The baseline oracle is checked in; tests do not depend on local git history.
Production renderTextView, textLayoutKey, copy budget and reset are extracted
verbatim. Deterministic fixture metrics/draw operations replace hardware only.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def verify_lifecycle():
    host = (ROOT / "src/native/NativeAppHost.cpp").read_text()
    entry_signature = ("static esp_err_t runNativeAppImpl(" if "static esp_err_t runNativeAppImpl(" in host
                       else "esp_err_t runNativeApp(")
    run = function(host, entry_signature)
    # Admission failure still crosses normal cleanup; owner identity is checked
    # before every public render method can mutate the single invocation cache.
    entry = run.index("session = &active;")
    launch = run.index("launch_elf_app(", entry)
    assert "nativeUiResetTextLayout();" in run[entry:launch]
    detach = run.index("session = nullptr;", launch)
    assert "nativeUiResetTextLayout();" in run[detach:run.index("nativeSettingsEnd();", detach)]
    owner = function(host, "Session* current()")
    assert "session->owner == xTaskGetCurrentTaskHandle()" in owner
    assert "!session->presenting" in owner
    bridge = (ROOT / "src/native/NativeUiBridge.cpp").read_text()
    for signature in ("void renderList(", "void renderTable("):
        render = function(bridge, signature)
        assert render.index("textViewCache.clear();") < render.index("r->clearScreen();")
    getter = function(bridge, 'extern "C" const t5_ui_api_v1* t5_ui_get_api(')
    assert "!active()" in getter
    assert getter.index("!active()") < getter.index("nativeUiResetTextLayout();")
    print("Native text cache invocation/owner/alternate-surface cleanup wiring PASS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="Enable fatal ASan and UBSan")
    args = parser.parse_args()
    flags = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    flags += ["-I" + str(ROOT / "src/native"), "-I" + str(HERE)]
    source = (ROOT / "src/native/NativeUiBridge.cpp").read_text()
    fixture = '#include "render_fixture.h"\n'
    theme = (ROOT / "src/components/themes/BaseTheme.cpp").read_text()
    fixture += function(theme, "int BaseTheme::resolveTextFontId(") + "\n"
    fixture += 'namespace old { HitLayout hitLayout;\n#include "baseline_render_oracle.inc"\n}\n'
    fixture += 'namespace current { HitLayout hitLayout; NativeTextLayoutCache textViewCache;\n'
    fixture += function(source, "NativeTextLayoutCache::Key textLayoutKey(") + "\n"
    fixture += function(source, "struct TextLayoutCopyBudget") + ";\n"
    fixture += function(source, "void renderTextView(") + "\n"
    fixture += function(source, "void nativeUiResetTextLayout()") + "\n}\n"
    fixture += '#include "render_test.inc"\n'
    verify_lifecycle()
    with tempfile.TemporaryDirectory(prefix="native-text-layout-") as temp:
        temp = Path(temp)
        generated = temp / "render_test.cpp"
        generated.write_text(fixture)
        for name, cpp in (("cache", HERE / "cache_test.cpp"), ("render", generated)):
            binary = temp / name
            subprocess.run(flags + [str(cpp), "-o", str(binary)], check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
