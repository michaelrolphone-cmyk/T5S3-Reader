#!/usr/bin/env python3
"""Wire the actual Driver Manager online path to canonical physical ELF packages.

The legacy release catalog remains a compatibility source; canonical physical
providers are preferred for identical driver IDs. No firmware publish or
activation is performed by the installer.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / 'src/native/NativeDriverManagerBridge.cpp'


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f'Expected one integration marker, observed {source.count(old)}: {old[:110]!r}')
    return source.replace(old, new, 1)


def replace_section(source: str, start: str, end: str, value: str) -> str:
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError(f'Unexpected Driver Manager structure: {start!r}')
    a = source.index(start)
    b = source.index(end, a)
    return source[:a] + value + source[b:]


CANONICAL = '''
// Canonical release assets are separate from old .t5driver.elf files. A
// physical provider is only advertised when its four-file inventory can be
// fetched and verified by the same transaction used by the SD inbox.
bool loadCanonicalDriverCatalog() {
    std::string json;
    if (!HttpDownloader::fetchUrl(kCanonicalProviderCatalogUrl, json) ||
        json.empty() || json.size() > kMaxDriverCatalogBytes) return false;
    JsonDocument document;
    if (deserializeJson(document, json) || !document.is<JsonObjectConst>() ||
        document["schema"] != 1 || !document["packages"].is<JsonArrayConst>())
        return false;
    const JsonArrayConst entries = document["packages"].as<JsonArrayConst>();
    if (entries.empty() || entries.size() > kMaxDriverAssets) return false;
    std::vector<CatalogDriver> found;
    found.reserve(entries.size());
    for (JsonVariantConst entry : entries) {
        if (!entry.is<JsonObjectConst>() || !entry["id"].is<const char*>() ||
            !entry["version"].is<const char*>() ||
            !entry["capability"].is<const char*>() ||
            !entry["api"].is<unsigned>() ||
            !entry["files"].is<JsonArrayConst>()) return false;
        const char* id = entry["id"].as<const char*>();
        const char* version = entry["version"].as<const char*>();
        const char* capability = entry["capability"].as<const char*>();
        if (!RuntimePackages::safeId(id) || !RuntimePackages::safeVersion(version) ||
            !RuntimePackages::safePackageCapability(capability) ||
            entry["api"].as<unsigned>() == 0) return false;
        uint32_t parts[3]{};
        if (!RuntimePackages::parsePackageVersion(version, parts)) return false;
        const JsonArrayConst files = entry["files"].as<JsonArrayConst>();
        if (files.size() != 4) return false;
        bool elf = false, descriptor = false, profile = false, imports = false;
        uint64_t size = 0;
        for (JsonVariantConst file : files) {
            if (!file.is<JsonObjectConst>() || !file["name"].is<const char*>() ||
                !file["size_bytes"].is<uint64_t>() ||
                !file["sha256"].is<const char*>()) return false;
            const char* name = file["name"].as<const char*>();
            const char* digest = file["sha256"].as<const char*>();
            const uint64_t bytes = file["size_bytes"].as<uint64_t>();
            if (!RuntimePackages::validSha256Hex(digest) || !bytes ||
                bytes > 8u * 1024u * 1024u) return false;
            if (!std::strcmp(name, "driver.elf")) { if (elf) return false; elf = true; size = bytes; }
            else if (!std::strcmp(name, ".package.json")) { if (descriptor || bytes > 4096) return false; descriptor = true; }
            else if (!std::strcmp(name, "provider-abi.v1")) { if (profile) return false; profile = true; }
            else if (!std::strcmp(name, "privileged-imports.v1")) { if (imports) return false; imports = true; }
            else return false;
        }
        if (!elf || !descriptor || !profile || !imports || size > UINT32_MAX ||
            std::any_of(found.begin(), found.end(), [id](const CatalogDriver& candidate) {
                return std::strcmp(candidate.info.id, id) == 0;
            })) return false;
        CatalogDriver candidate;
        std::snprintf(candidate.info.id, sizeof(candidate.info.id), "%s", id);
        std::snprintf(candidate.info.version, sizeof(candidate.info.version), "%s", version);
        std::snprintf(candidate.info.capability, sizeof(candidate.info.capability), "%s", capability);
        candidate.info.sizeBytes = static_cast<uint32_t>(size);
        serializeJson(entry, candidate.canonicalMetadata);
        if (candidate.canonicalMetadata.empty() || candidate.canonicalMetadata.size() > 8192) return false;
        found.push_back(std::move(candidate));
    }
    catalog.swap(found);
    sortCatalog();
    LOG_INF("DRVMGR", "Discovered %u canonical hardware-owning driver packages",
            static_cast<unsigned>(catalog.size()));
    return true;
}

'''

REFRESH = '''bool catalogRefresh() {
    if (!activeNativeApp()) return false;
    ManagerMutation mutation;
    if (!mutation) return false;
    catalog.clear();
    if (!connectSavedWifi()) return false;
    const bool canonical = loadCanonicalDriverCatalog();
    std::vector<CatalogDriver> physical;
    if (canonical) physical.swap(catalog);
    catalog.clear();
    bool legacy = loadAggregateDriverCatalog();
    if (!legacy) {
        catalog.clear();
        legacy = loadLegacyReleaseCatalog();
    }
    // A canonical physical ELF always supersedes a same-ID legacy proxy.
    // Preserve unrelated old releases, including GPS, during the transition.
    for (auto& driver : physical) {
        catalog.erase(std::remove_if(catalog.begin(), catalog.end(),
            [&driver](const CatalogDriver& existing) {
                return !std::strcmp(existing.info.id, driver.info.id);
            }), catalog.end());
        catalog.push_back(std::move(driver));
    }
    sortCatalog();
    return canonical || legacy;
}

'''

VERSION = '''bool installedVersionGet(const char* id, char* version, size_t capacity) {
    if (!activeNativeApp() || !version || !capacity || !RuntimePackages::safeId(id)) return false;
    version[0] = 0;
    const std::string canonicalPath = std::string("/Drivers/") + id;
    constexpr RuntimePackages::PackageRuntimePolicy policy{
        "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
    RuntimePackages::Identity observed{};
    if (RuntimePackages::verifyOrdinarySdDirectory(canonicalPath.c_str(), policy,
            RuntimePackages::installedCapabilityVersion, observed) &&
        observed.kind == RuntimePackages::Kind::Driver &&
        !std::strcmp(observed.id, id)) {
        const size_t length = std::strlen(observed.version);
        if (length >= capacity) return false;
        std::memcpy(version, observed.version, length + 1);
        return true;
    }
    return getInstalledDriverVersion(id, version, capacity);
}

'''


def main() -> None:
    source = PATH.read_text(encoding='utf-8')
    if 'bool loadCanonicalDriverCatalog()' in source:
        print('Driver Manager canonical online integration already applied')
        return
    source = replace_once(source, '#include <T5DriverManagerApi.h>\n',
                          '#include <T5DriverManagerApi.h>\n#include "NativeOnlineDriverInstall.h"\n')
    source = replace_once(source, '#include "runtime/packages/PackageOrdinaryTransaction.h"\n',
                          '#include "runtime/packages/PackageOrdinaryTransaction.h"\n'
                          '#include "runtime/packages/PackageOrdinarySdAdapter.h"\n'
                          '#include "runtime/packages/InstalledCapabilityResolver.h"\n')
    source = replace_once(source,
        'constexpr const char* kDriverCatalogUrl =\n',
        'constexpr const char* kCanonicalProviderCatalogUrl =\n'
        '    "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/usb-provider-catalog.json";\n'
        'constexpr const char* kDriverCatalogUrl =\n')
    source = replace_once(source,
        '    std::string elfUrl;\n};\n\nstruct RecoveryItem {',
        '    std::string elfUrl;\n    std::string canonicalMetadata;\n};\n\nstruct RecoveryItem {')
    source = replace_section(source, '\nbool catalogRefresh() {',
                             '\nuint32_t catalogCount() {', '\n' + CANONICAL + REFRESH)
    source = replace_section(source, '\nbool installedVersionGet(', '\nbool install(uint32_t index) {',
                             '\n' + VERSION)
    source = replace_once(source,
        '    const CatalogDriver selected = catalog[index];\n',
        '''    const CatalogDriver selected = catalog[index];
    if (!selected.canonicalMetadata.empty()) {
        char installed[T5_DRIVER_VERSION_MAX]{};
        if (installedVersionGet(selected.info.id, installed, sizeof(installed)) &&
            RuntimePackages::comparePackageVersions(selected.info.version, installed) !=
                RuntimePackages::VersionOrder::Newer) return false;
        return RuntimeOnlinePackages::DriverIntake::install(selected.info.id,
            selected.info.version, selected.canonicalMetadata);
    }
''')
    PATH.write_text(source, encoding='utf-8')
    print('Integrated online canonical drivers, actual installer and version lookup into native Driver Manager')


if __name__ == '__main__':
    main()
