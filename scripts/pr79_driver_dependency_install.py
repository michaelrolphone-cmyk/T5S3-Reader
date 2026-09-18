#!/usr/bin/env python3
"""Install all physical USB ELF prerequisites when a user installs a driver.

A selected USB serial class driver recursively resolves its capability graph
from the canonical release index. Installation runs under the Driver Manager's
existing mutation reservation and never activates hardware or bypasses the
ordinary package validator. Missing/circular dependencies fail closed.
"""
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / 'src/native/NativeDriverManagerBridge.cpp'

INSERT = '''// Installing a serial class driver must also install its provider graph;
// installing just a leaf ELF cannot produce a usable serial.port capability.
// Use the release index's declared requirements to install prerequisites in
// dependency order, then let the shared transaction validate each package.
bool installCanonicalDependencies(size_t index, std::vector<uint8_t>& visiting) {
    if (index >= catalog.size() || index >= visiting.size() ||
        catalog[index].canonicalMetadata.empty() || visiting[index] == 1) return false;
    if (visiting[index] == 2) return true;
    visiting[index] = 1;
    const CatalogDriver& selected = catalog[index];
    JsonDocument metadata;
    if (deserializeJson(metadata, selected.canonicalMetadata) ||
        !metadata.is<JsonObjectConst>() || !metadata["requires"].is<JsonArrayConst>())
        return false;
    const JsonArrayConst requirements = metadata["requires"].as<JsonArrayConst>();
    if (requirements.size() > RuntimePackages::kMaxPackageRequirements) return false;
    for (JsonVariantConst requirement : requirements) {
        if (!requirement.is<JsonObjectConst>() ||
            !requirement["capability"].is<const char*>() ||
            !requirement["min_api"].is<unsigned>()) return false;
        const char* capability = requirement["capability"].as<const char*>();
        const uint32_t minimumApi = requirement["min_api"].as<unsigned>();
        if (!RuntimePackages::safePackageCapability(capability) || !minimumApi) return false;
        if (RuntimePackages::installedCapabilityVersion(capability) >= minimumApi) continue;
        size_t prerequisite = catalog.size();
        for (size_t candidate = 0; candidate < catalog.size(); ++candidate) {
            if (catalog[candidate].canonicalMetadata.empty() ||
                std::strcmp(catalog[candidate].info.capability, capability)) continue;
            JsonDocument provider;
            if (deserializeJson(provider, catalog[candidate].canonicalMetadata) ||
                !provider["api"].is<unsigned>() ||
                provider["api"].as<unsigned>() < minimumApi) continue;
            prerequisite = candidate;
            break;
        }
        if (prerequisite == catalog.size()) {
            LOG_ERR("DRVMGR", "Required physical provider unavailable: %s", capability);
            return false;
        }
        LOG_INF("DRVMGR", "Installing %s required by %s",
                catalog[prerequisite].info.id, selected.info.id);
        if (!installCanonicalDependencies(prerequisite, visiting) ||
            RuntimePackages::installedCapabilityVersion(capability) < minimumApi)
            return false;
    }
    char installed[T5_DRIVER_VERSION_MAX]{};
    if (installedVersionGet(selected.info.id, installed, sizeof(installed))) {
        const auto order = RuntimePackages::comparePackageVersions(selected.info.version, installed);
        if (order == RuntimePackages::VersionOrder::Equal) {
            visiting[index] = 2;
            return true;
        }
        if (order != RuntimePackages::VersionOrder::Newer) return false;
    }
    if (!RuntimeOnlinePackages::DriverIntake::install(selected.info.id,
            selected.info.version, selected.canonicalMetadata)) return false;
    visiting[index] = 2;
    return true;
}

'''


def main() -> None:
    before = PATH.read_text(encoding='utf-8')
    if 'bool installCanonicalDependencies(' in before:
        print('Canonical dependency installation already integrated')
        return
    marker = 'bool install(uint32_t index) {\n'
    if before.count(marker) != 1:
        raise SystemExit('Missing actual Driver Manager installer')
    source = before.replace(marker, INSERT + marker, 1)
    old = '''        return RuntimeOnlinePackages::DriverIntake::install(selected.info.id,
            selected.info.version, selected.canonicalMetadata);
'''
    new = '''        std::vector<uint8_t> visiting(catalog.size(), 0);
        return installCanonicalDependencies(index, visiting);
'''
    if source.count(old) != 1:
        raise SystemExit('Native canonical release installer not present; refusing partial patch')
    source = source.replace(old, new, 1)
    PATH.write_text(source, encoding='utf-8')
    print('Driver Manager now installs physical USB dependencies automatically before selected driver')


if __name__ == '__main__':
    main()
