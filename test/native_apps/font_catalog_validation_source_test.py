#!/usr/bin/env python3
"""Ensure the live font bridge validates a complete staged catalog."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/native/NativeFontBridge.cpp").read_text()
start = source.index("t5_font_result_t refreshCatalog()")
end = source.index("uint32_t familyCount()", start)
refresh = source[start:end]

assert 'doc["families"].is<JsonArray>()' in refresh
assert 'fObj["files"].is<JsonArray>()' in refresh
assert "std::vector<ManifestFamily> candidateFamilies" in refresh
assert "candidateFamilies.push_back" in refresh
assert "FontCatalogValidation::publish" in refresh
assert "families.clear()" not in refresh
assert "baseUrl.swap(candidateBaseUrl)" in refresh
assert "families[index]" not in refresh  # parsing cannot mutate live entries

print("font catalog validation source contracts passed")
