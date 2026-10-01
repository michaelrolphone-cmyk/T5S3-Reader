#!/usr/bin/env python3
"""Emit compact source/target reference evidence during a normal firmware build.

Observation only: indirect calls are recorded, never asserted fully resolved.
No firmware execution, publication, release or artifact retrieval occurs here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
ROOT_NAMES = ('t5_usb_get_api', 't5_serial_port_get_api', 'installedAcquirePort',
              'acquirePort', 'openUsb', 'nativeSerialProviderRead', 'nativeSerialProviderWrite',
              'nativeProviderOwnerTick', 't5_serial_port_get_api_original',
              'ensureUsbRegistered', 'acquireInstalled', 'diagnosticAcquire')
LEGACY_NAMES = ('usbAcquirePort', 'UsbSerialProjection', 'NativeUsbDevices',
                'nativeUsbDirectStreamClaim', 'nativeUsbProviderAttach', 'nativeUsbClassRead',
                'nativeUsbClassWrite', 'usb_host_install', 'hcd_port_init')


def references(disassembly, read_virtual, objects, functions):
    direct = sorted(set(re.findall(r'\bcall(?:0|4|8|12)\b[^\n]*?<([^\n]+)>[ \t]*$', disassembly, re.MULTILINE)))
    indirect = [line.strip() for line in disassembly.splitlines() if re.search(r'\bcallx(?:0|4|8|12)\b', line)]
    literals = []
    for text in sorted(set(re.findall(r'\bl32r\s+\w+,\s*(?:0x)?([0-9a-fA-F]+)\b', disassembly))):
        address = int(text, 16)
        raw = read_virtual(address, 4)
        if raw is None:
            continue
        value, = struct.unpack('<I', raw)
        item = {'literal_address': hex(address), 'value': hex(value)}
        if value in functions:
            item['function_target'] = functions[value]
        tables = []
        for obj in objects.get(value, []):
            size = obj['size']
            if size > 4096 or size % 4:
                continue
            data = read_virtual(value, size)
            if data is None:
                continue
            pointers = []
            for offset in range(0, size, 4):
                pointer, = struct.unpack_from('<I', data, offset)
                if pointer in functions:
                    pointers.append({'byte_offset': offset, 'address': hex(pointer), 'symbols': functions[pointer]})
            if pointers:
                tables.append({'symbol': obj['name'], 'size': size, 'function_references': pointers})
        if tables:
            item['referenced_objects'] = tables
        literals.append(item)
    return {'direct_calls': direct, 'indirect_call_instructions': indirect, 'literal_references': literals}


def report(path, objdump, source_head):
    from elftools.elf.elffile import ELFFile
    deadline = time.monotonic() + 120
    if not path.is_file() or not 52 <= path.stat().st_size <= 256 * 1024 * 1024:
        raise ValueError('firmware ELF missing or outside report bound')
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(65536), b''):
            digest.update(data)
        stream.seek(0)
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_XTENSA':
            raise ValueError('expected linked Xtensa ELF32 firmware')
        table = elf.get_section_by_name('.symtab')
        if table is None or table.num_symbols() > 100000:
            raise ValueError('missing or oversized linked symbol table')
        symbols = [symbol for symbol in table.iter_symbols() if symbol.name and symbol['st_shndx'] != 'SHN_UNDEF']
        names = [symbol.name for symbol in symbols]
        demangled = subprocess.run([str(objdump).replace('objdump', 'c++filt')], input='\n'.join(names)+'\n',
                                   text=True, capture_output=True, check=True, timeout=60).stdout.splitlines()
        if len(demangled) != len(names):
            raise ValueError('symbol demangling was incomplete')
        functions, objects, selected, legacy = {}, {}, [], []
        for symbol, name in zip(symbols, demangled):
            address, size = symbol['st_value'], symbol['st_size']
            item = {'name': name, 'mangled': symbol.name, 'address': address, 'size': size}
            if any(token in name for token in LEGACY_NAMES):
                legacy.append(item)
            if symbol['st_info']['type'] == 'STT_FUNC':
                functions.setdefault(address, []).append(name)
                if size and any(re.search(r'\b'+token+r'\b', name) for token in ROOT_NAMES):
                    selected.append(item)
            elif symbol['st_info']['type'] == 'STT_OBJECT':
                objects.setdefault(address, []).append(item)
        if not any(item['name'] == 't5_serial_port_get_api' for item in selected):
            raise ValueError('linked serial API entrypoint not found')
        if len(selected) > 32 or len(legacy) > 512:
            raise ValueError('reference report exceeds symbol bound')
        sections = [section for section in elf.iter_sections()
                    if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS']

        def read_virtual(address, size):
            for section in sections:
                start = section['sh_addr']
                if start <= address and address + size <= start + section['sh_size']:
                    stream.seek(section['sh_offset'] + address - start)
                    value = stream.read(size)
                    return value if len(value) == size else None
            return None

        for item in selected:
            if item['size'] > 65536:
                raise ValueError('selected reference root exceeds disassembly bound')
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('target reference report deadline exceeded')
            assembly = subprocess.run([str(objdump), '-d', '-C',
                '--start-address='+hex(item['address']), '--stop-address='+hex(item['address']+item['size']),
                str(path)], text=True, capture_output=True, check=True, timeout=min(15, remaining)).stdout
            if len(assembly) > 512 * 1024:
                raise ValueError('selected disassembly exceeds report bound')
            item['disassembly'] = assembly
            item['references'] = references(assembly, read_virtual, objects, functions)
    source_files = ['platformio.ini', 'src/native/NativeUsbBridge.cpp',
                    'src/native/NativeSerialPortBridge.cpp',
                    'src/native/NativeSerialPortBridge_implementation.inc', 'src/native/NativeStreamBridge.p1.inc',
                    'src/runtime/drivers/InstalledSerialSession.h', 'lib/NativeApps/include/T5SerialPortApi.h']
    sources = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in source_files}
    return {'schema': 1, 'compiled_checkout': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'requested_head': source_head, 'firmware_elf_sha256': digest.hexdigest(), 'source_sha256': sources,
            'legacy_symbol_observations': legacy, 'entrypoint_references': selected,
            'limits': 'Direct call/literal/API-object references plus source hashes; indirect calls are not fully resolved; no physical PHY result.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-head', required=True)
    parser.add_argument('--objdump', type=Path, default=Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home()/'.platformio')) /
                        'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-objdump')
    args = parser.parse_args()
    result = report(args.elf, args.objdump, args.source_head)
    payload = json.dumps(result, indent=2) + '\n'
    if len(payload.encode()) > 2 * 1024 * 1024:
        raise ValueError('compact reference report exceeds two MiB')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(payload)
    print(f"Target reference evidence: {len(result['entrypoint_references'])} roots, "
          f"{len(result['legacy_symbol_observations'])} legacy-name observations; review required")
