#!/usr/bin/env python3
"""Production KOReader loaders with real ArduinoJson; synthetic in-memory SD only.

The complete credential store and Serialization.h are compiled unchanged. JSON
codec/settings callback and sync dispatch are extracted verbatim to avoid mocking
these decisions while excluding unrelated firmware networking and rendering.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("json_include", type=Path)
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
ROOT = Path(os.environ.get("KOREADER_TEST_ROOT", Path(__file__).resolve().parents[2]))
HERE = Path(__file__).resolve().parent


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


codec = (ROOT / "src/JsonSettingsIO.cpp").read_text()
bridge = (ROOT / "src/native/NativeKOReaderBridge.cpp").read_text()
sync = (ROOT / "src/activities/reader/KOReaderSyncActivity.cpp").read_text()
start = sync.index("  // Calculate document hash", sync.index("void KOReaderSyncActivity::performSync()"))
end = sync.index("  if (documentHash.empty())", start)
source = r'''
#include <ArduinoJson.h>
#include <KOReaderCredentialStore.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>
#include <T5KOReaderApi.h>
#include <cassert>
#include <cstring>
#include <sstream>
#include <iostream>
bool active() { return true; }
struct KOReaderDocumentId {
 static std::string calculateFromFilename(const std::string&) { return "filename-id"; }
 static std::string calculate(const std::string&) { return "binary-id"; }
};
'''
source += function(codec, "bool JsonSettingsIO::saveKOReader(") + "\n"
source += function(codec, "bool JsonSettingsIO::loadKOReader(") + "\n"
source += function(bridge, "void copyText(") + "\n"
source += function(bridge, "bool readSettings(") + "\n"
source += "std::string selectedHash() { std::string documentHash, epubPath = \"/book.epub\";\n"
source += sync[start:end] + "return documentHash; }\n"
source += (HERE / "cases.cpp").read_text()
with tempfile.TemporaryDirectory(prefix="koreader-match-") as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    binary = Path(tmp) / "test"
    flags = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-function"]  # Serialization.h contains unused static overloads.
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    includes = [HERE / "stubs", ROOT / "lib/KOReaderSync", ROOT / "lib/Serialization",
                ROOT / "lib/NativeApps/include", args.json_include.resolve()]
    command = flags + ["-I" + str(p) for p in includes]
    command += [str(ROOT / "lib/KOReaderSync/KOReaderCredentialStore.cpp"), str(cpp), "-o", str(binary)]
    subprocess.run(command, check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
