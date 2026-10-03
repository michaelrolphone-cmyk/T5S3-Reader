#!/usr/bin/env python3
"""Check decoding of direct calls, indirect calls and actual table pointers."""
from pathlib import Path
import struct
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from report_u1_target_references import references, REQUIRED_ENTRYPOINTS
# Keep the strict linked-symbol check aligned with the authoritative declaration:
# its default argument does not create an old zero-argument ELF entrypoint.
assert REQUIRED_ENTRYPOINTS['x4-diagnostic'] == 'x4DiagnosticSetup(bool)'
assert 'void x4DiagnosticSetup(bool deskClockUserWake = false);' in (ROOT/'src/platform/X4DiagnosticBoot.h').read_text()
assert REQUIRED_ENTRYPOINTS['controller'] == 't5_driver_get'
assert REQUIRED_ENTRYPOINTS['firmware'] == 't5_serial_port_get_api'
memory = {0x1000: struct.pack('<I', 0x2000), 0x1004: struct.pack('<I', 0x3000),
          0x2000: struct.pack('<IIII', 1, 16, 0x3000, 0x4000)}
functions = {0x3000: ['installedAcquirePort'], 0x4000: ['releasePort']}
objects = {0x2000: [{'name': 'serialApi', 'size': 16}]}
result = references('100: call8 3000 <installedAcquirePort>\n104: callx8 a3\n'
                    '108: l32r a2, 1000 <literal>\n10c: l32r a3, 0x1004 <literal>\n',
                    lambda address, size: memory.get(address), objects, functions)
assert result['direct_calls'] == ['installedAcquirePort']
assert len(result['indirect_call_instructions']) == 1
assert result['literal_references'][0]['referenced_objects'][0]['function_references'] == [
    {'byte_offset': 8, 'address': '0x3000', 'symbols': ['installedAcquirePort']},
    {'byte_offset': 12, 'address': '0x4000', 'symbols': ['releasePort']}]
assert result['literal_references'][1]['function_target'] == ['installedAcquirePort']
assert references('call8 4000 <bool Registry::copy<40u>(char (&)[40u])>\n', lambda a,n: None, {}, {})['direct_calls'] == ['bool Registry::copy<40u>(char (&)[40u])']
assert references('l32r a2, 9999', lambda a,n: None, objects, functions)['literal_references'] == []
print('Target evidence decoder: direct/indirect calls, literal values and object function-pointer references PASS')
