"""Bound the pinned Arduino 2.0.17 module-store SPI port; fail on source drift.

Only global FSPI is guarded. Existing SPI/parameter mutexes remain the owners.
No controller abort/reset is added. A failed owner is retained until reboot.
"""
from pathlib import Path
import hashlib
import json
import re
MARK = '// RiscRTE SD/SPI retained-fault port v1'
PATCHED = {'hal': 'f7d14845900aac311cf728f3416b5ce32f4ff205489b01d444e729c1ce59f37c', 'spi': '98e19cea2fa17fb283157d3e3e49e66db543c6e6fa13f693b54bacca7c4edc4c', 'sd': '427725199401ad3a6254d6b993d9db371d1141627a3490c7a81c985fde29bd64', 'vfs': 'cba3ea1a987b9ae0f6efe42d9adc031484fb3147093262e93560fc43108bc2ba'}
EXPECTED = {
    'hal': 'c635b836e8e77412f3fa5e7bf7196a477eb86d99',
    'spi': '3af07515c2be974168ddb683802f784f182b1769',
    'sd': 'da967338589e71d18517d6137ecc8a573b438d13',
    'vfs': '1dd8da94ac93cc6bf5e4f5bcc6854ab4fdef5481',
}

def baseline(text, kind):
    # The connector/test materialization may add an empty final line.
    candidates = [(text.rstrip('\n')+'\n'*n).encode() for n in range(6)]
    digests = [hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() for data in candidates]
    digest = digests[0]
    if EXPECTED[kind] not in digests:
        raise RuntimeError('Pinned Arduino '+kind+' source drift: '+digest)

def patch(text, kind):
    if MARK in text:
        if hashlib.sha256((text.rstrip()+'\n').encode()).hexdigest() != PATCHED[kind]:
            raise RuntimeError('Patched Arduino '+kind+' source drift')
        return text
    baseline(text, kind)
    text = MARK+'\n#include "SdSpiFault.h"\n#include "RuntimeFaultRetention.h"\n'+text
    if kind == 'hal':
        old = '#define SPI_MUTEX_LOCK()    do {} while (xSemaphoreTake(spi->lock, portMAX_DELAY) != pdPASS)'
        assert text.count(old)==1
        text=text.replace(old, '#define SPI_MUTEX_LOCK() do { if (spi->num == FSPI) risc_sd_spi_wait_lock(spi->lock); else { do {} while (xSemaphoreTake(spi->lock, portMAX_DELAY) != pdPASS); } } while (0)')
        text=text.replace('#define SPI_MUTEX_UNLOCK()  xSemaphoreGive(spi->lock)', '#define SPI_MUTEX_UNLOCK() do { if (spi->num == FSPI) risc_sd_spi_guard(); xSemaphoreGive(spi->lock); } while (0)')
        # StartBus previously reset the controller before taking its mutex.
        # Move FSPI acquisition ahead of that first side effect, without adding
        # another bus owner or changing the other controller's lock policy.
        begin=text.index('spi_t * spiStartBus(')
        end=text.index('void spiWaitReady(', begin)
        section=text[begin:end]
        anchor='#if CONFIG_IDF_TARGET_ESP32S2\n    if(spi_num == FSPI)'
        assert section.count(anchor)==1
        section=section.replace(anchor, '    if (spi->num == FSPI) SPI_MUTEX_LOCK();\n\n'+anchor)
        section=section.replace('    SPI_MUTEX_LOCK();\n    spiInitBus(spi);', '    if (spi->num != FSPI) SPI_MUTEX_LOCK();\n    spiInitBus(spi);')
        text=text[:begin]+section+text[end:]
        text=text.replace('#include "esp32-hal.h"', '#include "esp32-hal.h"\n#if CONFIG_DISABLE_HAL_LOCKS\n#error RiscRTE retained SPI fault policy requires the existing HAL mutexes\n#endif')
        # All public/private pointer entrypoints, including no-lock operations,
        # refuse before GPIO/register writes. NULL behavior stays unchanged.
        pattern=r'(^[^\n;{}]*\([^\n;{}]*spi_t \* spi[^\n;{}]*\)\s*\{)'
        text,n=re.subn(pattern, lambda m:m[1]+'\n    if (spi && spi->num == FSPI) risc_sd_spi_guard();', text, flags=re.M)
        if n != 47: raise RuntimeError('SPI entrypoint count changed: '+str(n))
        for declaration in ('    spi_t * spi = &_spi_bus_array[spi_num];', '    spi_t * spi = (spi_t *)arg;'):
            assert text.count(declaration)==1
            text=text.replace(declaration,declaration+'\n    if (spi && spi->num == FSPI) risc_sd_spi_guard();')
        # Both update and usr can stick. One wait has a fixed start, while the
        # enclosing operation deadline survives every sector/retry/transaction.
        pattern=r'while\s*\(spi->dev->cmd\.(usr|update)\);'
        def wait(m):
            flag=m[1]
            return 'do { const uint32_t began = xTaskGetTickCount(); while (spi->dev->cmd.'+flag+') { if (spi->num == FSPI) risc_sd_spi_busy(began); } } while (0);'
        text,n=re.subn(pattern,wait,text)
        if n!=41: raise RuntimeError('SPI wait count changed: '+str(n))
        text,n=re.subn(r'(while\((?:size|len)\)\s*\{)', r'\1 if (spi->num == FSPI) risc_sd_spi_guard();', text)
        if n!=4: raise RuntimeError('SPI chunk loop count changed: '+str(n))
    elif kind == 'spi':
        old='#define SPI_PARAM_LOCK()    do {} while (xSemaphoreTake(paramLock, portMAX_DELAY) != pdPASS)'
        assert text.count(old)==1
        text=text.replace(old, '#define SPI_PARAM_LOCK() do { if (_spi_num == FSPI) { risc_sd_spi_begin_operation(); risc_sd_spi_wait_lock(paramLock); } else { do {} while (xSemaphoreTake(paramLock, portMAX_DELAY) != pdPASS); } } while (0)')
        text=text.replace('#define SPI_PARAM_UNLOCK()  xSemaphoreGive(paramLock)', '#define SPI_PARAM_UNLOCK() do { if (_spi_num == FSPI) risc_sd_spi_end_operation(); xSemaphoreGive(paramLock); } while (0)')
        pattern=r'(^[^\n;{}]*SPIClass::(?:~?\w+)\([^\n]*\)\s*\{)'
        text,n=re.subn(pattern,lambda m:m[1]+'\n    SdSpiOperation riscOperation(_spi_num == FSPI);',text,flags=re.M)
        if n!=24: raise RuntimeError('SPIClass entrypoint count changed: '+str(n))
    elif kind == 'vfs':
        # Opaque raw File/FS handles can hide buffered SD I/O. Guard before
        # libc locks, fclose/free and implicit shared_ptr destruction as well
        # as the disk layer. No raw VFS cleanup can outlive this fault barrier.
        pattern=r'(^[^\n;{}]*VFS(?:File)?Impl::[^\n(]+\([^\n]*\)(?: const)?\s*(?::[^{}]*)?\{)'
        text,n=re.subn(pattern,lambda m:m[1]+'\n    risc_runtime_retention_guard();',text,flags=re.M)
        if n!=28: raise RuntimeError('VFS entrypoint count changed: '+str(n))
    else:
        # Raw SDFS/File compatibility uses Arduino's disk adapter, not SdFat.
        # Keep its sector retries in the same whole-operation observation.
        pattern=r'(^[A-Za-z_][^\n;{}]+\([^\n;{}]*\)\s*\{)'
        text,n=re.subn(pattern,lambda m:m[1]+'\n    SdSpiOperation riscOperation;',text,flags=re.M)
        if n!=27: raise RuntimeError('SD disk entrypoint count changed: '+str(n))
    return text

def original_source(text, kind):
    if MARK not in text:
        baseline(text, kind)
        return text
    patch(text, kind)  # Accept only our exact known output before reverting it.
    text=text.replace(MARK+'\n#include "SdSpiFault.h"\n#include "RuntimeFaultRetention.h"\n', '', 1)
    if kind == 'hal':
        text=text.replace('#include "esp32-hal.h"\n#if CONFIG_DISABLE_HAL_LOCKS\n#error RiscRTE retained SPI fault policy requires the existing HAL mutexes\n#endif', '#include "esp32-hal.h"')
        text=text.replace('#define SPI_MUTEX_LOCK() do { if (spi->num == FSPI) risc_sd_spi_wait_lock(spi->lock); else { do {} while (xSemaphoreTake(spi->lock, portMAX_DELAY) != pdPASS); } } while (0)', '#define SPI_MUTEX_LOCK()    do {} while (xSemaphoreTake(spi->lock, portMAX_DELAY) != pdPASS)')
        text=text.replace('#define SPI_MUTEX_UNLOCK() do { if (spi->num == FSPI) risc_sd_spi_guard(); xSemaphoreGive(spi->lock); } while (0)', '#define SPI_MUTEX_UNLOCK()  xSemaphoreGive(spi->lock)')
        text=text.replace('\n    if (spi && spi->num == FSPI) risc_sd_spi_guard();', '')
        text=text.replace('    if (spi->num == FSPI) SPI_MUTEX_LOCK();\n\n', '')
        text=text.replace('    if (spi->num != FSPI) SPI_MUTEX_LOCK();\n    spiInitBus(spi);', '    SPI_MUTEX_LOCK();\n    spiInitBus(spi);')
        for flag in ('usr', 'update'):
            replacement='while'+(' ' if flag == 'update' else '')+'(spi->dev->cmd.'+flag+');'
            text=text.replace('do { const uint32_t began = xTaskGetTickCount(); while (spi->dev->cmd.'+flag+') { if (spi->num == FSPI) risc_sd_spi_busy(began); } } while (0);', replacement)
        text=text.replace(' if (spi->num == FSPI) risc_sd_spi_guard();', '')
    elif kind == 'spi':
        text=text.replace('\n    SdSpiOperation riscOperation(_spi_num == FSPI);', '')
        text=text.replace('#define SPI_PARAM_LOCK() do { if (_spi_num == FSPI) { risc_sd_spi_begin_operation(); risc_sd_spi_wait_lock(paramLock); } else { do {} while (xSemaphoreTake(paramLock, portMAX_DELAY) != pdPASS); } } while (0)', '#define SPI_PARAM_LOCK()    do {} while (xSemaphoreTake(paramLock, portMAX_DELAY) != pdPASS)')
        text=text.replace('#define SPI_PARAM_UNLOCK() do { if (_spi_num == FSPI) risc_sd_spi_end_operation(); xSemaphoreGive(paramLock); } while (0)', '#define SPI_PARAM_UNLOCK()  xSemaphoreGive(paramLock)')
    elif kind == 'vfs':
        text=text.replace('\n    risc_runtime_retention_guard();', '')
    else:
        text=text.replace('\n    SdSpiOperation riscOperation;', '')
    baseline(text, kind)  # Never restore an approximate or guessed baseline.
    return text


def patch_environment(env):
    framework=Path(env.PioPlatform().get_package_dir('framework-arduinoespressif32'))
    files={'hal':framework/'cores/esp32/esp32-hal-spi.c', 'spi':framework/'libraries/SPI/src/SPI.cpp', 'sd':framework/'libraries/SD/src/sd_diskio.cpp', 'vfs':framework/'libraries/FS/src/vfs_api.cpp'}
    originals={kind: path.read_text() for kind,path in files.items()}
    try:
        clean={kind: original_source(text,kind) for kind,text in originals.items()}
        updates={kind: patch(clean[kind],kind) for kind in files}
    except RuntimeError:
        if any(len(text.encode()) > 200000 for text in originals.values()):
            raise RuntimeError('SDK source drift exceeds diagnostic bound')
        report=Path(env.subst('$PROJECT_DIR'))/'dist/u1-sdk-source-drift.json'
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps({'schema':1,'files':originals},indent=2)+'\n')
        raise
    project=Path(env.subst('$PROJECT_DIR'))
    overlay=Path(env.subst('$BUILD_DIR'))/'u1-sdk-sources'
    overlay.mkdir(parents=True, exist_ok=True)
    companions={name:(project/'lib/hal'/name).read_text()
                for name in ('SdSpiFault.h','RuntimeFaultRetention.h')}
    for name,content in companions.items():
        old=framework/'cores/esp32'/name
        if old.exists() and old.read_text()!=content:
            raise RuntimeError('Unexpected shared SDK companion drift: '+name)
    for name,content in companions.items():
        (overlay/name).write_text(content)
        old=framework/'cores/esp32'/name
        if old.exists(): old.unlink()  # Only our exact verified private companion.
    replacements={}
    report={'schema':1,'original_git_blobs':EXPECTED,'compiled_units':{}}
    report_path=overlay/'build-selection.json'
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    for kind,path in files.items():
        # Migration only: clean our previously published, hash-verified patch
        # out of a restored global SDK cache. Normal builds never edit it.
        if originals[kind]!=clean[kind]: path.write_text(clean[kind])
        replacement=overlay/path.name
        replacement.write_text(updates[kind])
        replacements[str(path.resolve())]=replacement

    def substitute(build_env,node):
        original=Path(node.srcnode().get_abspath())
        replacement=replacements.get(str(original.resolve()))
        if replacement is None: return node
        isolated=build_env.Clone()
        isolated.Prepend(CPPPATH=[str(overlay),str(original.parent)])
        report['compiled_units'][original.name]=hashlib.sha256(replacement.read_bytes()).hexdigest()
        report_path.write_text(json.dumps(report,indent=2)+'\n')
        return isolated.Object(target=str(overlay/'objects'/(original.name+'.o')),source=str(replacement))

    # PRE registration is required: framework/library source nodes are already
    # constructed by the time PlatformIO executes post scripts.
    env.AddBuildMiddleware(substitute)

if 'Import' in globals():
    Import('env')
    patch_environment(env)
