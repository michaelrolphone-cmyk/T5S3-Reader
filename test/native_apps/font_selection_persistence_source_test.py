#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "src/native/NativeFontBridge.cpp").read_text(encoding="utf-8")
start = source.index("t5_font_result_t selectChoice(uint32_t index)")
end = source.index("\nconst t5_font_api_v1 api", start)
body = source[start:end]

snapshot_font = body.index("const uint8_t previousFontFamily = SETTINGS.fontFamily;")
snapshot_sd = body.index("std::memcpy(previousSdFontFamilyName, SETTINGS.sdFontFamilyName")
save = body.index("if (!SETTINGS.saveToFile())")
restore_font = body.index("SETTINGS.fontFamily = previousFontFamily;", save)
restore_sd = body.index("std::memcpy(SETTINGS.sdFontFamilyName, previousSdFontFamilyName", save)
storage_error = body.index("return T5_FONT_STORAGE_ERROR;", save)
success = body.index("return T5_FONT_OK;", save)

assert snapshot_font < save
assert snapshot_sd < save
assert save < restore_font < storage_error
assert save < restore_sd < storage_error
assert body.count("ensureSdFontLoaded();") == 2
assert storage_error < success
assert "return T5_FONT_OK;" not in body[:save]

print("Font selection persistence/rollback source contract PASS")
