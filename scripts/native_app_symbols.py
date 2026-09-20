"""Check ELF imports against actual firmware symbol tables and scoped port ABI.

The privileged inventory is deliberately NOT merged into firmware_exports():
normal application ELF relocations must not be granted OS/CPU primitives.
"""
import pathlib
import re


def native_hardware_compat_exports(repo):
    """Exact, temporarily registered direct-hardware imports on T5S3.

    Require the firmware table to include the same .def inventory in its
    declarations AND initializers; a handwritten wish list is not an export.
    """
    repo = pathlib.Path(repo)
    source = (repo / 'src/native/NativeHardwareCompat.cpp').read_text()
    definition = 'NativeHardwareCompatSymbols.def'
    if source.count(f'#include "{definition}"') != 2 or (
            'native_hardware_compat_register' not in source or
            'native_hardware_compat_unregister' not in source):
        raise ValueError('Native hardware compatibility table is missing or malformed')
    entries = (repo / 'lib/NativeApps/include' / definition).read_text()
    names = re.findall(r'^RISC_COMPAT_SYMBOL\((\w+)\)\s*$', entries, re.M)
    if not names or len(names) != len(set(names)):
        raise ValueError('Empty or duplicate native hardware compatibility exports')
    return set(names)


def firmware_exports(repo):
    repo = pathlib.Path(repo)
    tables = [
        (repo / 'lib/NativeApps/src/NativeAppLauncher.c', 'host_symbols'),
        (repo / 'lib/elf_loader/src/esp_elf_symbol.c', 'g_esp_libc_elfsyms'),
    ]
    exports = set()
    for path, table in tables:
        source = path.read_text()
        match = re.search(r'\b' + table + r'\s*\[\s*\]\s*=\s*\{(.*?)\n\s*\};', source, re.S)
        if not match:
            raise ValueError(f'Cannot locate firmware symbol table {table}')
        exports.update(re.findall(r'ESP_ELFSYM_EXPORT\(\s*(\w+)\s*\)', match[1]))
    flags = (repo / 'platformio.ini').read_text()
    if '-DCONFIG_ELF_LOADER_LIBC_SYMBOLS=1' not in flags:
        raise ValueError('Native import validation requires the configured libc export table')
    return exports | native_hardware_compat_exports(repo)


def privileged_os_cpu_exports(repo):
    """Exact ABI-v1 names resolvable ONLY during an admitted provider load."""
    repo = pathlib.Path(repo)
    source = (repo / 'lib/elf_loader/include/private/privileged_os_cpu_symbols_v1.def').read_text()
    names = re.findall(r'^RISC_OS_CPU_SYMBOL\((\w+)\)\s*$', source, re.M)
    if not names or len(names) != len(set(names)):
        raise ValueError('Empty or duplicate privileged OS/CPU ABI inventory')
    if any(n.startswith(('t5_', 'usb_', 'usbh_', 'hcd_', 'hub_', 'i2c_',
                         'gpio_', 'periph_module_')) for n in names):
        raise ValueError('Peripheral or app symbol is not an OS/CPU primitive')
    return set(names)


def privileged_loader_public_libc_v1(repo):
    """Actual independent provider preflight is intentionally narrower than
    the ordinary application resolver: registered apps and other modules must
    never accidentally satisfy a physical driver's undefined import.
    """
    repo = pathlib.Path(repo)
    source = (repo / 'lib/elf_loader/src/esp_privileged_imports.c').read_text()
    match = re.search(r'\bs_public_libc\s*\[\s*\]\s*=\s*\{(.*?)\n\};', source, re.S)
    if not match or '#include "private/privileged_os_cpu_symbols_v1.def"' not in source:
        raise ValueError('Private loader import inventory is missing or malformed')
    names = re.findall(r'"([A-Za-z_][A-Za-z_0-9]*)"', match[1])
    if not names or len(names) != len(set(names)):
        raise ValueError('Empty or duplicate private loader public libc names')
    # Firmware exports() parses all alternative libc configurations in source,
    # not only one current target's preprocessor branch.
    missing = set(names) - firmware_exports(repo)
    if missing:
        raise ValueError('Private preflight claims unavailable libc symbols: ' +
                         ', '.join(sorted(missing)))
    return set(names)


def validate_imports(symbol_listing, exports):
    required = set()
    for line in symbol_listing.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[4] == 'GLOBAL' and fields[6] == 'UND':
            required.add(fields[7])
    missing = sorted(required - exports)
    if missing:
        raise ValueError('ELF imports symbols not exported by firmware: ' + ', '.join(missing))
    return required
