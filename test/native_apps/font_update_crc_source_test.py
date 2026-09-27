#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "src/native/NativeFontBridge.cpp").read_text(encoding="utf-8")

compute_start = source.index("bool computeCrc32(const char* path, uint32_t& out)")
compute_end = source.index("\nt5_font_result_t refreshCatalog()", compute_start)
compute = source[compute_start:compute_end]
assert "if (n <= 0)" in compute
assert "return false;" in compute

refresh_start = source.index("t5_font_result_t refreshCatalog()")
refresh_end = source.index("\nuint32_t familyCount()", refresh_start)
refresh = source[refresh_start:refresh_end]

size_check = refresh.index("if (actual != entry.size)")
crc_decl = refresh.index("uint32_t installedCrc = 0;")
crc_check = refresh.index("!computeCrc32(path, installedCrc) || installedCrc != entry.crc32")
update_mark = refresh.index("family.hasUpdate = true;", crc_decl)

assert size_check < crc_decl < crc_check < update_mark

def has_update(expected_size, actual_size, expected_crc, actual_crc=None, crc_ok=True):
    if actual_size != expected_size:
        return True
    if not crc_ok:
        return True
    return actual_crc != expected_crc

assert has_update(100, 101, 0x1234, 0x1234)
assert has_update(100, 100, 0x1234, 0x5678)
assert not has_update(100, 100, 0x1234, 0x1234)
assert has_update(100, 100, 0x1234, None, crc_ok=False)

print("Font update CRC detection contract PASS")
