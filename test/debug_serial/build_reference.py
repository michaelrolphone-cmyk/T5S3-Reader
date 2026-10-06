#!/usr/bin/env python3
"""One-time fixture refresh, requiring the exact pinned SDK and source checkout."""
from pathlib import Path
import hashlib, json, subprocess, sys
out=Path(__file__).resolve().parent
repo=out.parents[1]
sdk=Path(sys.argv[1])
def function(text, signature):
    start=text.index(signature); begin=text.index('{',start); depth=1; end=begin+1
    while depth:
        depth += (text[end]=='{') - (text[end]=='}'); end+=1
    return text[start:end]
base='b91bc323fb736eb6a13ea1a9403470cf699c6128'
main=subprocess.check_output(['git','show',f'{base}:src/main.cpp'],cwd=repo,text=True)
old=main[main.index('  // Handle incoming serial commands,'):main.index('  // Check for any user activity',main.index('  // Handle incoming serial commands,'))]
(out/'original_dispatch.inc').write_text(old.rstrip()+'\n')
pieces=[]; hashes={}
for name,sigs in {
 'Stream.cpp':['int Stream::timedRead()','String Stream::readStringUntil(char terminator)'],
 'WString.cpp':['bool String::equals(const char *cstr) const','void String::trim(void)','String String::substring(unsigned int left, unsigned int right) const'],
 'HWCDC.cpp':['int HWCDC::available(void)','int HWCDC::read(void)'],
}.items():
    text=(sdk/name).read_text();hashes[name]=hashlib.sha256(text.encode()).hexdigest()
    if text.startswith('/*'):pieces.append(text[:text.index('*/')+2])
    for sig in sigs: pieces.append(function(text,sig))
(out/'sdk_reference.inc').write_text('// Exact functions from Arduino ESP32 2.0.17 (LGPL-2.1-or-later).\n// Copyright Arduino / Espressif contributors; see upstream source URLs in README.md.\n\n'+'\n\n'.join(pieces)+'\n')
manifest={'baseline':base,'sdk_package':'3.20017.241212+sha.dcc1105b','source_sha256':hashes,'main_sha256':hashlib.sha256(main.encode()).hexdigest(),'fixtures':{p:hashlib.sha256((out/p).read_bytes()).hexdigest() for p in ['original_dispatch.inc','sdk_reference.inc']}}
(out/'reference.json').write_text(json.dumps(manifest,indent=2)+'\n')
