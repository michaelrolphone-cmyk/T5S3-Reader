from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from patch_m5gfx_lifecycle import patch_sources,patch_wisp_source
fixture=ROOT/'test/fixtures/m5gfx-0.2.20'
h,c=patch_sources((fixture/'Panel_EPD.hpp').read_text(),(fixture/'Panel_EPD.cpp').read_text())
c=patch_wisp_source(c)
assert patch_wisp_source(c)==c
assert 'epd_quality || new_data.mode == epd_mode_t::epd_text' in c
assert 'if (!spatial_done)' in c
assert c.index('M5WispRefresh::run')<c.index('bool flg_fast =')
with tempfile.TemporaryDirectory() as temp:
    binary=Path(temp)/'test'
    subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'src/native'),'-I'+str(ROOT/'lib/hal'),
                    str(ROOT/'test/hal/static_wisp_refresh_test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=10)
