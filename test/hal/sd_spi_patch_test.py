#!/usr/bin/env python3
"""Check the exact pinned port patch and execute its real SPI busy-wait body."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from patch_sd_spi_fault import patch, MARK
if len(sys.argv)==2:
    framework=Path(sys.argv[1])
    paths={'hal':framework/'cores/esp32/esp32-hal-spi.c', 'spi':framework/'libraries/SPI/src/SPI.cpp', 'sd':framework/'libraries/SD/src/sd_diskio.cpp', 'vfs':framework/'libraries/FS/src/vfs_api.cpp'}
else:
    assert len(sys.argv)==5, 'Pass actual framework directory, or exact upstream HAL/SPI/SD/VFS sources'
    paths=dict(zip(('hal','spi','sd','vfs'),map(Path,sys.argv[1:])))
texts={}
for kind,path in paths.items():
    source=path.read_text(); result=patch(source,kind); assert patch(result,kind)==result
    if MARK not in source:
        try: patch(source.replace('Copyright','ChangedCopyright',1),kind)
        except RuntimeError: pass
        else: raise AssertionError('Source drift was accepted')
    texts[kind]=result
hal=texts['hal']; spi=texts['spi']; sd=texts['sd']
assert hal.count('risc_sd_spi_busy(began)')==40
assert hal.count('if (spi && spi->num == FSPI) risc_sd_spi_guard();')==49
start=hal.split('spi_t * spiStartBus(',1)[1].split('void spiWaitReady(',1)[0]
assert start.index('if (spi->num == FSPI) SPI_MUTEX_LOCK();') < start.index('periph_module_reset(')
assert 'risc_sd_spi_wait_lock(spi->lock)' in hal and 'risc_sd_spi_wait_lock(paramLock)' in spi
assert spi.count('SdSpiOperation riscOperation(_spi_num == FSPI);')==24
assert sd.count('SdSpiOperation riscOperation;')==27
assert texts['vfs'].count('risc_runtime_retention_guard();')==28
# Exact production wait, including guard-before-access and fixed-start loop.
body='void spiWaitReady('+hal.split('void spiWaitReady(',1)[1].split('\nvoid spiWrite(',1)[0]
assert body.count('void ')==1
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    (tmp/'actual_spi_wait.inc').write_text('''
#define FSPI 0
struct spi_dev_t { struct { unsigned usr; } cmd; };
struct spi_t { spi_dev_t *dev; unsigned num; };
'''+body)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                    '-DRISCRTE_SD_SPI_FAULT_TEST','-DRISC_ACTUAL_SPI_WAIT',
                    '-I'+str(ROOT/'test/hal/spi_fault_stubs'),'-I'+str(ROOT/'lib/hal'),'-I'+str(tmp),
                    str(ROOT/'lib/hal/SdSpiFault.cpp'),str(ROOT/'test/hal/sd_spi_fault_test.cpp'),
                    '-o',str(tmp/'actual-wait')],check=True)
    subprocess.run([str(tmp/'actual-wait'),'actual-wait'],check=True,timeout=10)
print('Pinned SPI/SD patch: exact source, all waits/entries, no pre-lock reset and actual stuck-register retention PASS')
# Execute the actual raw VFS close body: rejection must precede even cached
# path/free/stdio teardown, not only a later SD-sector callback.
body='void VFSFileImpl::close()'+texts['vfs'].split('void VFSFileImpl::close()',1)[1].split('VFSFileImpl::operator bool()',1)[0]
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    prelude=r'''
#include "RuntimeFaultRetention.h"
#include <cassert>
#include <csetjmp>
#include <cstddef>
static bool fault;
static unsigned released;
static std::jmp_buf stopped;
extern "C" void risc_runtime_retention_guard() { if(fault) std::longjmp(stopped,1); }
static void counted_free(void*) { ++released; }
static int counted_close(void*) { ++released; return 0; }
struct VFSFileImpl { char *_path; bool _isDirectory; void *_d; void *_f; void close(); };
#define free counted_free
#define closedir counted_close
#define fclose counted_close
'''
    main=r'''
int main() {
 char path[]="/owned";
 VFSFileImpl file{path,false,nullptr,path};
 fault=true;
 if(!setjmp(stopped)) { file.close(); assert(false); }
 assert(!released && file._path==path && file._f==path);
 fault=false; file.close(); assert(released==2 && !file._path && !file._f);
}
'''
    (tmp/'close.cpp').write_text(prelude+body+main)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-DRISCRTE_SD_SPI_FAULT_TEST',
                    '-I'+str(ROOT/'lib/hal'),str(tmp/'close.cpp'),'-o',str(tmp/'close')],check=True)
    subprocess.run([str(tmp/'close')],check=True,timeout=10)
print('Actual raw VFS close: fault precedes free/stdio teardown; ordinary close preserved PASS')
