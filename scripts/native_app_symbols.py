"""Check ELF imports against the firmware's enabled native-app export tables."""
import pathlib
import re


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
    # Both firmware targets enable the loader's default libc symbol table.
    # ESP-IDF/customer tables are not enabled and must not be treated as exports.
    flags = (repo / 'platformio.ini').read_text()
    if '-DCONFIG_ELF_LOADER_LIBC_SYMBOLS=1' not in flags:
        raise ValueError('Native import validation requires the configured libc export table')
    return exports


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
