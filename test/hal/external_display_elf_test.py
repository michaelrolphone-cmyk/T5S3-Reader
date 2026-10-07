#!/usr/bin/env python3
"""Assert the actual T5 firmware has no resident LCD/EPD engine; one shared SDK DMA authority."""
from pathlib import Path
import hashlib,sys
from elftools.elf.elffile import ELFFile
if len(sys.argv)!=2:raise SystemExit('Pass one actual T5 firmware ELF')
path=Path(sys.argv[1])
if path.stat().st_size>256*1024*1024:raise ValueError('Firmware ELF exceeds bound')
with path.open('rb') as stream:
 elf=ELFFile(stream);assert elf['e_machine']=='EM_XTENSA' and elf['e_type']=='ET_EXEC'
 table=elf.get_section_by_name('.symtab');assert table and table.num_symbols()<1000000
 names={s.name for s in table.iter_symbols() if s['st_shndx']!='SHN_UNDEF'}
 forbidden=sorted(n for n in names if n.startswith(('esp_lcd_','lcd_ll_','lcd_hal_')) or
                  any(part in n for part in ('9Panel_EPD','7Bus_EPD','epd_video_init','epd_video_shutdown','lut_2pixel')))
 assert not forbidden,'Resident display controller/waveform owner: '+repr(forbidden)
 assert 'risc_cpu_dma_reserve_tx_v3' in names and 'gdma_new_channel' in names
 assert 't5_video_get_api' in names,'Legacy capability consumer missing'
 stream.seek(0);digest=hashlib.sha256()
 for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
 print('No resident LCD/EPD engine; shared SDK DMA reservation present; legacy consumer retained: PASS sha256='+digest.hexdigest())
