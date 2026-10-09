#!/usr/bin/env python3
"""Read actual controller imports and exact linked Runtime lookup tables.

This does not add a symbol, admit a provider, or establish physical operation.
The optional baseline comparison ignores debug-only source-line changes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile

def symbols(elf):
    table=elf.get_section_by_name('.symtab')
    if table is None:raise ValueError('Unstripped Runtime ELF required')
    return {symbol.name:symbol for symbol in table.iter_symbols() if symbol['st_shndx']!='SHN_UNDEF'}

def address_bytes(elf,address,size):
    for section in elf.iter_sections():
        if section['sh_flags']&2 and section['sh_addr']<=address and address+size<=section['sh_addr']+section['sh_size']:
            offset=address-section['sh_addr'];return section.data()[offset:offset+size]
    raise ValueError(f'Address {address:#x} is not in one allocated section')

def text_at(elf,address):
    out=bytearray()
    for offset in range(256):
        value=address_bytes(elf,address+offset,1)[0]
        if not value:return out.decode('ascii')
        out.append(value)
    raise ValueError('Unbounded symbol name')

def lookup_table(elf,syms,name):
    symbol=syms.get(name)
    if symbol is None:return {}
    size=symbol['st_size']
    if not size or size%8:raise ValueError('Unexpected 32-bit lookup table: '+name)
    result={}
    for name_address,function in struct.iter_unpack('<II',address_bytes(elf,symbol['st_value'],size)):
        if not name_address:
            if function:raise ValueError('Malformed table terminator')
            continue
        key=text_at(elf,name_address)
        if key in result or not function:raise ValueError('Duplicate or unresolved table entry '+key)
        result[key]=function
    return result

def allocated(elf):
    return {section.name:{'type':section['sh_type'],'flags':section['sh_flags'],
        'address':section['sh_addr'],'size':section['sh_size'],
        'sha256':hashlib.sha256(section.data()).hexdigest()}
        for section in elf.iter_sections() if section['sh_flags']&2}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--controller',type=Path,required=True)
    parser.add_argument('--runtime-elf',type=Path,required=True)
    parser.add_argument('--runtime-source',type=Path,required=True)
    parser.add_argument('--baseline',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    with args.controller.open('rb') as stream:
        controller=ELFFile(stream)
        imports=sorted({symbol.name for symbol in controller.get_section_by_name('.dynsym').iter_symbols()
            if symbol.name and symbol['st_shndx']=='SHN_UNDEF' and symbol['st_info']['bind'] in ('STB_GLOBAL','STB_WEAK')})
        sections=allocated(controller)
        controller_symbols=symbols(controller)
        native_selected=any('nativePhyLease' in name for name in controller_symbols)
        interfaces=[symbol for name,symbol in controller_symbols.items() if name.endswith('13hid_interfaceE')]
        if len(interfaces)!=1:raise ValueError('Expected exact controller diagnostics table')
        advertised=struct.unpack('<I',address_bytes(controller,interfaces[0]['st_value']+4,4))[0]
    with args.runtime_elf.open('rb') as stream:
        runtime=ELFFile(stream);syms=symbols(runtime)
        public={}
        for name in ('g_esp_libc_elfsyms','g_esp_espidf_elfsyms','g_customer_elfsyms'):
            public.update(lookup_table(runtime,syms,name))
        private=lookup_table(runtime,syms,'s_privileged_symbols_v1')
        # Runtime's scoped resolver substitutes these only when their weak
        # functions actually resolve; a symbol string alone proves nothing.
        for name,target in (('printf','risc_provider_diagnostic_printf'),('puts','risc_provider_diagnostic_puts')):
            if target in syms and syms[target]['st_value']:private[name]=syms[target]['st_value']
        marker=syms.get('risc_usb_phy_resource_enabled')
        enabled=bool(marker and marker['st_size']==4 and struct.unpack('<I',address_bytes(runtime,marker['st_value'],4))[0]==1)
        absent=sorted(set(imports)-set(syms))
    module=(args.runtime_source/'src/runtime/drivers/ProviderModuleV2.cpp').read_text()
    load=module[module.index('bool ModuleV2::load('):module.index('bool ModuleV2::loadVerifiedBytes(')]
    verified=module[module.index('bool ModuleV2::loadVerifiedBytes('):module.index('bool ModuleV2::poll(')]
    graph=(args.runtime_source/'src/runtime/drivers/ProviderGraphV2.cpp').read_text()
    admission=graph[graph.index('bool GraphV2::addManagerValidatedPrivileged('):graph.index('bool GraphV2::addChecked(')]
    out={
        'controller':str(args.controller),'controller_sha256':hashlib.sha256(args.controller.read_bytes()).hexdigest(),
        'runtime_elf':str(args.runtime_elf),'runtime_sha256':hashlib.sha256(args.runtime_elf.read_bytes()).hexdigest(),
        'imports':imports,'import_count':len(imports),
        'native_phy_lease_selected':native_selected,'advertised_controller_bytes':advertised,
        'resolved_by_actual_public_table':sorted(set(imports)&set(public)),
        'missing_actual_public_table':sorted(set(imports)-set(public)),
        'resolved_by_actual_private_table':sorted(set(imports)&set(private)),
        'missing_even_with_private_scope':sorted(set(imports)-set(public)-set(private)),
        'named_symbols_absent_from_runtime_symtab':absent,
        'symbol_name_note':'A missing symbol-table name is not an unresolved import if the actual lookup table contains its name and a nonzero address.',
        'native_phy_enabled_in_runtime_binary':enabled,
        'normal_module_uses_dlopen': 'esp_dlopen_instance(path)' in load and 'dlopen(path, RTLD_NOW)' in load,
        'verified_module_is_disabled': 'return false;' in verified and 'esp_elf_relocate_privileged' not in verified and '(void)candidateBytes;' in verified,
        'privileged_graph_admission_is_disabled':'(void)spec; return false;' in admission,
        'controller_native_contract_change':False,
        'activation_qualified':False,
    }
    if args.baseline:
        with args.baseline.open('rb') as stream:before=allocated(ELFFile(stream))
        out['baseline_allocated_sections_identical']=before==sections
        out['baseline_changed_allocated_sections']=sorted(name for name in set(before)|set(sections) if before.get(name)!=sections.get(name))
    args.output.write_text(json.dumps(out,indent=2)+'\n')
    print(json.dumps({key:out[key] for key in ('import_count','native_phy_lease_selected','advertised_controller_bytes','missing_even_with_private_scope','native_phy_enabled_in_runtime_binary','normal_module_uses_dlopen','verified_module_is_disabled')},indent=2))
    if args.baseline:print('Default allocated sections unchanged:',out['baseline_allocated_sections_identical'])

if __name__=='__main__':main()
