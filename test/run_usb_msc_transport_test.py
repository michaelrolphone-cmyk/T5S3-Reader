#!/usr/bin/env python3
"""Execute production stop/wait bodies against simulated MMIO and pinned SDK layout.
No hardware or target build. --soc-include is the ESP32-S3 SDK include directory.
"""
import argparse, os, re, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--soc-include',type=Path,required=True)
a=p.parse_args()
src=(root/'Drivers/usb_device_msc_esp32s3/transport.c').read_text()
def function(signature):
 start=src.index(signature);brace=src.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(src[end]=='{')-(src[end]=='}');end+=1
 return src[start:end]
harness=(root/'test/usb_device_msc/transport_stop_test.c').read_text()
harness=harness.replace('/* PRODUCTION_WAIT */',function('static bool wait_bits(').replace('wait_bits(', 'production_wait_bits(',1))
harness=harness.replace('/* PRODUCTION_STOP */',function('bool risc_msc_transport_stop('))
header=(a.soc_include/'soc/usb_wrap_struct.h').read_text()
for name,bit in [('test_rx_rcv',4),('test_rx_dp',5),('test_rx_dm',6)]:
 assert re.search(rf'{name}\s*:\s*RO; bitpos: \[{bit}\]',header),name
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'test.c';f.write_text(harness);exe=Path(d)/'test'
 flags=['-fsanitize=undefined','-fno-sanitize-recover=all'] if os.getenv('SANITIZE')=='1' else []
 subprocess.run([os.getenv('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',*flags,'-I'+str(root),'-I'+str(a.soc_include),str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=20)
