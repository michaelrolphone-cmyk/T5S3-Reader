"""Complete production metadata builder/ZIP regression; fixture storage and clock."""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile,zipfile,warnings
ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path(os.environ.get('EPUB_SPINE_SOURCE_ROOT',ROOT))
BASELINE=os.environ.get('EPUB_SPINE_BASELINE')=='1'
EXPECT_OPTIMIZED=not BASELINE or os.environ.get('EPUB_SPINE_EXPECT_OPTIMIZED')=='1'
TEST=ROOT/'test/epub_spine_sizes'
with tempfile.TemporaryDirectory(prefix='epub-spine-size-') as tmp:
    build=Path(tmp)
    (build/'freertos').mkdir()
    (build/'freertos/FreeRTOS.h').write_text('#pragma once\n#include <cstdint>\n#include <cstdlib>\n')
    (build/'freertos/task.h').write_text('#pragma once\nvoid vTaskDelay(int);\n')
    (build/'Arduino.h').write_text('#pragma once\n#include <cstdint>\nuint32_t millis();\nvoid vTaskDelay(int);\n')
    (build/'Logging.h').write_text('#pragma once\ntemplate<class... T> inline void logDiscard(T&&...){}\n#define LOG_ERR(...) logDiscard(__VA_ARGS__)\n#define LOG_DBG(...) logDiscard(__VA_ARGS__)\n')
    (build/'FsHelpers.h').write_text('#pragma once\n#include <string>\nnamespace FsHelpers {std::string normalisePath(const std::string&);}\n')
    s=(SOURCE/'lib/FsHelpers/FsHelpers.cpp').read_text();a=s.index('std::string normalisePath(');b=s.index('\n}',a)+2
    (build/'normalise.inc').write_text(s[a:b])
    rows=[]
    fixtures=[]
    for n,extra,method in [(1,8,0),(7,16,0),(8,16,0),(16,64,0),(64,256,0),(127,512,0),(128,512,0),(127,512,8),(127,897,0),(127,898,0),(8,1016,0)]:
        for order in ['forward','reverse','front']:
            reverse=order=='reverse'
            path=build/f'{n}-{extra}-{method}-{order}.epub'
            chapters=[(f'OPS/ch{i:04d}.xhtml',bytes([65+i%26])*(256+i)) for i in range(n)]
            aux=[(f'OPS/aux{i:04d}.xhtml',b'aux') for i in range(extra)]
            entries=chapters+aux if order=='front' else aux+(chapters[::-1] if reverse else chapters)
            with zipfile.ZipFile(path,'w',compression=method) as z:
                for name,data in entries:z.writestr(name,data)
            fixtures.append((n,extra,method,order,path))
    special=build/'special.zip'
    with warnings.catch_warnings():
        warnings.simplefilter('ignore',UserWarning)
        with zipfile.ZipFile(special,'w') as z:
            for name,size in [('dup',11),('unique',23),('dup',37),('zero',0),('nulx',7),('x'*256,1)]:z.writestr(name,b'a'*size)
    data=special.read_bytes();data=data.replace(b'nulx',b'nul\0');special.write_bytes(data)
    for variant in ['normal','asan']:
        flags=['-O1','-g','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror']
        if variant=='asan':flags+=['-fsanitize=address']
        # Existing canonical BUG256 owns ZIP EOCD unaligned typed loads; no UBSan claim here.
        obj=build/(variant+'-tinf.o')
        subprocess.run(['cc','-std=c11',*flags,'-I'+str(SOURCE/'lib/uzlib/src'),'-c',str(SOURCE/'lib/uzlib/src/tinflate.c'),'-o',str(obj)],check=True)
        exe=build/(variant+'-test')
        subprocess.run(['c++','-std=c++17',*flags,'-Wno-unused-function','-fno-access-control',*(['-DBASELINE_SOURCE'] if BASELINE else []),'-include','cstdlib',*['-I'+str(p) for p in [build,TEST,SOURCE/'lib/Epub/Epub',SOURCE/'lib/Serialization',SOURCE/'lib/ZipFile',SOURCE/'lib/InflateReader']],str(TEST/'size_lookup_test.cpp'),str(SOURCE/'lib/Epub/Epub/BookMetadataCache.cpp'),str(SOURCE/'lib/ZipFile/ZipFile.cpp'),str(SOURCE/'lib/InflateReader/InflateReader.cpp'),str(obj),'-Wl,--gc-sections','-o',str(exe)],check=True)
        for n,extra,method,order,path in fixtures:
            reverse=order=='reverse'
            modes=['normal']
            if n==127 and extra==512 and method==0 and reverse:
                modes+=['slow','rollover','normalize'] if BASELINE else ['allocation','metadata','seek','open','slow','timeout','rollover','normalize']
            for mode in modes:
                out=build/'book.bin'
                row=json.loads(subprocess.check_output([str(exe),str(path),str(n),mode,str(out)],text=True,timeout=45))
                eligible=8<=n<128 and n+extra<=1024
                if EXPECT_OPTIMIZED and eligible and mode in ['normal','slow','rollover','normalize']:
                    if reverse:
                        # First two baseline scans, one directory preparation,
                        # then two reads per exact hinted record.
                        assert row['reads']==20*(n+extra)+2*n-10,row
                        assert row['entries']==3*(n+extra)+n-3,row
                    else:
                        needed=n if order=='front' else n+extra
                        assert row['reads']==9*needed+1,row
                        assert row['allocation_bytes']==0,row
                if mode in ['slow','rollover']:assert row['yields']>0,row
                row|={'variant':variant,'mode':mode,'extra':extra,'method':method,'reverse':reverse,'order':order,'sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
                rows.append(row);print(json.dumps(row),flush=True)
        if not BASELINE:subprocess.run([str(exe),str(special),'6','direct',str(build/'unused')],check=True,timeout=45)
    # Every compression/order change must retain all serialized metadata bytes.
    for n in {r['spine'] for r in rows}:
        assert len({r['sha256'] for r in rows if r['spine']==n and r['mode']!='normalize'})==1
    dest=os.environ.get('EPUB_SPINE_RESULTS')
    if dest:Path(dest).write_text(json.dumps(rows,indent=2))
    print('PASS: complete production small-spine ZIP lookup, exact output and bounded fallback/lifetime')
