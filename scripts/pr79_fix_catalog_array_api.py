#!/usr/bin/env python3
"""Use ArduinoJson's supported size() rather than std::vector empty()."""
from pathlib import Path
path = Path(__file__).resolve().parents[1] / 'src/native/NativeDriverManagerBridge.cpp'
source = path.read_text(encoding='utf-8')
old = 'if (entries.empty() || entries.size() > kMaxDriverAssets) return false;'
new = 'if (entries.size() == 0 || entries.size() > kMaxDriverAssets) return false;'
if source.count(old) == 1:
    path.write_text(source.replace(old, new), encoding='utf-8')
    print('Canonical catalog now uses supported JsonArrayConst::size()')
elif new in source:
    print('Canonical catalog array API already corrected')
else:
    raise SystemExit('Unexpected canonical catalog parser; refusing mutation')
