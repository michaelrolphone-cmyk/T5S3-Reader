from pathlib import Path
import json, subprocess, zipfile, os, tempfile, warnings

SRC = Path(__file__).resolve().parents[2]
BUILD_CONTEXT = tempfile.TemporaryDirectory(prefix='epub-css-')
BUILD = Path(BUILD_CONTEXT.name)
(BUILD/'freertos').mkdir(exist_ok=True)
(BUILD/'freertos/FreeRTOS.h').write_text('#pragma once\n#include <cstdint>\n#include <cstdlib>\n')
(BUILD/'freertos/task.h').write_text('#pragma once\nvoid vTaskDelay(int);\n')
(BUILD/'Logging.h').write_text('#pragma once\ntemplate<class... T> void cssLog(const char*,const char*,T...){}\n#define LOG_ERR(...) cssLog(__VA_ARGS__)\n#define LOG_DBG(...) cssLog(__VA_ARGS__)\n')
TEST = SRC/'test/epub_css'
normalizer=(SRC/'lib/FsHelpers/FsHelpers.cpp').read_text()
a=normalizer.index('std::string normalisePath(');b=normalizer.index('\n}',a)+2
(BUILD/'normalise.inc').write_text(normalizer[a:b]+'\n')
epub=Path(os.environ.get('EPUB_CSS_SOURCE', SRC/'lib/Epub/Epub.cpp')).read_text()
BASELINE = os.environ.get('EPUB_CSS_BASELINE') == '1'
def function(start):
    a=epub.index(start); b=epub.index('\n}',a)+2
    return epub[a:b]
extracted='\n\n'.join(function(s) for s in [
    'void Epub::parseCssFiles() const {',
    'bool Epub::readItemContentsToStream(',
    'bool Epub::getItemSize(',
])
(BUILD/'epub_functions.inc').write_text(extracted+'\n')
variants = ['normal', 'asan']
results=[]
for variant in variants:
    flags=['-Wall','-Wextra','-Werror','-O1','-g','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections']
    if variant=='asan': flags+=['-fsanitize=address']
    # UBSan is not part of this run: canonical BUG256 already owns EOCD unaligned loads.
    obj=BUILD/(variant+'-tinf.o')
    subprocess.run(['cc','-std=c11',*flags,'-I'+str(SRC/'lib/uzlib/src'),'-c',str(SRC/'lib/uzlib/src/tinflate.c'),'-o',str(obj)],check=True)
    exe=BUILD/(variant+'-probe')
    subprocess.run(['c++','-std=c++17',*flags,'-Wl,--gc-sections','-include','cstdlib','-I'+str(BUILD),'-I'+str(TEST),'-I'+str(SRC/'lib/Epub/Epub/css'),'-I'+str(SRC/'lib/ZipFile'),'-I'+str(SRC/'lib/InflateReader'),str(TEST/'selected_stats_test.cpp'),str(SRC/'lib/Epub/Epub/css/CssParser.cpp'),str(SRC/'lib/ZipFile/ZipFile.cpp'),str(SRC/'lib/InflateReader/InflateReader.cpp'),str(obj),'-o',str(exe)],check=True)
    for chapters, css_count, location, method in [(64,8,'tail',0),(256,8,'tail',0),(512,8,'tail',0),(512,16,'front',0),(512,16,'tail',0),(512,32,'tail',0),(1024,32,'tail',0),(512,16,'tail',8)]:
        path=BUILD/f'{chapters}-{css_count}-{location}-{method}.epub'
        entries=[]
        entries.append(('mimetype','application/epub+zip'))
        entries.append(('META-INF/container.xml','<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'))
        manifest=''.join(f'<item id="s{i}" href="style{i}.css" media-type="text/css"/>' for i in range(css_count))+''.join(f'<item id="c{i}" href="ch{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(chapters))
        spine=''.join(f'<itemref idref="c{i}"/>' for i in range(chapters))
        entries.append(('OEBPS/content.opf',f'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Performance fixture</dc:title><dc:identifier id="id">fixture</dc:identifier><dc:language>en</dc:language></metadata><manifest>{manifest}</manifest><spine>{spine}</spine></package>'))
        styles=[(f'OEBPS/style{i}.css',f'p {{ margin-left: {i}px; }}\n') for i in range(css_count)]
        books=[(f'OEBPS/ch{i}.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter</title></head><body><p>Chapter text</p></body></html>') for i in range(chapters)]
        entries += styles+books if location=='front' else books+styles
        with zipfile.ZipFile(path,'w',compression=method) as z:
            for name,data in entries:z.writestr(name,data,compress_type=0 if name=='mimetype' else method)
        env=os.environ.copy()  # Keep hosted leak detection; local ptrace opt-out comes from the caller.
        row=json.loads(subprocess.check_output([str(exe),str(path),str(css_count),'normal'],env=env,text=True))
        if not BASELINE:
            assert row['archive_read_calls'] <= 2*len(entries)+2*css_count+2, row
            assert row['opens']==row['closes']==1, row
            assert row['archive_entries_visited']==3+css_count+(chapters if location=='tail' else 0), row
        row|={'variant':variant,'chapters':chapters,'location':location,'method':method,'archive_bytes':path.stat().st_size,'logical_entries':len(entries)}
        results.append(row);print(json.dumps(row),flush=True)
    # Ordinary repeated member name: every repeated lookup must retain the first.
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', UserWarning)
        with zipfile.ZipFile(path,'a') as z:
            z.writestr('OEBPS/style0.css', 'p { margin-left: 999px; }\n')
    for mode in ['alloc1','alloc2','reverse','duplicates','missing','normalize','lowheap','threshold','release-heap','slow','timeout','rollover','metadata','seek','open','header','write','temp-read','temp-write']:
        row=json.loads(subprocess.check_output([str(exe),str(path),str(css_count),mode],env=env,text=True))
        if not BASELINE and mode in ['reverse','duplicates','normalize','slow','rollover']:
            assert row['archive_read_calls'] < 1200,row
        if mode=='lowheap':assert row['archive_read_calls']==0,row
        print(json.dumps(row),flush=True)
print('PASS: bounded production cold CSS metadata reuse; cached CSS performs no archive reads')
