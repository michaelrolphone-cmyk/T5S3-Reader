#pragma once

#include "NativeDriverInboxRecovery.h"
#include "network/HttpDownloader.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace RuntimeOnlinePackages {
namespace DriverIntake {
constexpr const char* kRelease =
    "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/";
constexpr const char* kNames[] = {
    ".package.json", "driver.elf", "provider-abi.v1", "privileged-imports.v1"};

inline bool hashMatches(const std::string& bytes, const char* hex) {
    if (!RuntimePackages::validSha256Hex(hex)) return false;
    uint8_t digest[32]{};
    if (mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(bytes.data()),
                           bytes.size(), digest, 0) != 0) return false;
    constexpr char alphabet[] = "0123456789abcdef";
    unsigned difference = 0;
    for (size_t i = 0; i < 32; ++i) {
        difference |= static_cast<unsigned>(hex[2 * i] != alphabet[digest[i] >> 4]);
        difference |= static_cast<unsigned>(hex[2 * i + 1] != alphabet[digest[i] & 15]);
    }
    return difference == 0;
}

inline bool writeExclusive(const std::string& path, const std::string& bytes) {
    HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL);
    if (!file.isOpen() || file.isDirectory()) {
        if (file.isOpen()) (void)file.close();
        return false;
    }
    const bool written = file.write(reinterpret_cast<const uint8_t*>(bytes.data()),
                                    bytes.size()) == bytes.size();
    return file.close() && written;
}

// The online catalog is only a locator and a set of expected checksums. Its
// four asset names are deterministic and never accepted as arbitrary paths.
// The downloaded .package.json is independently parsed; exact inventory,
// dependency preflight, ELF format and all hashes are rechecked by the same
// engine used for offline SD inbox installations.
inline bool install(const char* id, const char* version, const std::string& catalogEntry) {
    using namespace RuntimePackages;
    if (!Storage.ready() || !safeId(id) || !safeVersion(version) ||
        catalogEntry.empty() || catalogEntry.size() > 8192) return false;
    JsonDocument catalog;
    if (deserializeJson(catalog, catalogEntry) || !catalog.is<JsonObjectConst>() ||
        !catalog["id"].is<const char*>() || !catalog["version"].is<const char*>() ||
        std::strcmp(catalog["id"].as<const char*>(), id) ||
        std::strcmp(catalog["version"].as<const char*>(), version) ||
        !catalog["files"].is<JsonArrayConst>()) return false;
    const JsonArrayConst files = catalog["files"].as<JsonArrayConst>();
    if (files.size() != 4) return false;
    const char* expectedSha[4]{};
    uint64_t expectedSize[4]{};
    bool seen[4]{};
    for (JsonVariantConst file : files) {
        if (!file.is<JsonObjectConst>() || !file["name"].is<const char*>() ||
            !file["size_bytes"].is<uint64_t>() ||
            !file["sha256"].is<const char*>()) return false;
        const char* name = file["name"].as<const char*>();
        size_t slot = 0;
        for (; slot < 4 && std::strcmp(name, kNames[slot]); ++slot) {}
        if (slot == 4 || seen[slot]) return false;
        seen[slot] = true;
        expectedSize[slot] = file["size_bytes"].as<uint64_t>();
        expectedSha[slot] = file["sha256"].as<const char*>();
        if (!expectedSize[slot] || expectedSize[slot] > 8u * 1024u * 1024u ||
            !validSha256Hex(expectedSha[slot])) return false;
    }
    for (bool found : seen) if (!found) return false;
    if (expectedSize[0] > 4096) return false;
    const std::string prefix = std::string(kRelease) + id + "--";
    std::string descriptor;
    if (!HttpDownloader::fetchUrl(prefix + "package.json", descriptor) ||
        descriptor.size() != expectedSize[0] ||
        !hashMatches(descriptor, expectedSha[0])) return false;
    // Retain the bounded package plan in heap memory: the dependency caller
    // must not keep a multi-kilobyte local alive throughout nested SD stages.
    std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
    if (!plan || !parseOrdinaryManifest(descriptor.data(), descriptor.size(), *plan) ||
        plan->identity.kind != Kind::Driver || std::strcmp(plan->identity.id, id) ||
        std::strcmp(plan->identity.version, version) ||
        std::strcmp(plan->identity.artifact, "driver.elf") ||
        plan->entryCount != 3) return false;
    for (size_t i = 1; i < 4; ++i) {
        bool match = false;
        for (size_t j = 0; j < plan->entryCount; ++j) {
            const auto& entry = plan->entries[j];
            if (std::strcmp(entry.name, kNames[i])) continue;
            match = entry.sizeBytes == expectedSize[i] &&
                    std::strcmp(entry.sha256, expectedSha[i]) == 0 &&
                    entry.executable == (i == 1);
            break;
        }
        if (!match) return false;
    }
    const std::string root = std::string("/Packages/Inbox/") + id;
    if ((!Storage.exists("/Packages") && !Storage.mkdir("/Packages", false)) ||
        (!Storage.exists("/Packages/Inbox") && !Storage.mkdir("/Packages/Inbox", false)))
        return false;
    if (Storage.exists(root.c_str())) {
        if (!Recovery::discardMatchingDriverInbox(root, descriptor, kNames, expectedSize)) {
            LOG_ERR("DRVMGR", "Existing driver inbox differs from this release; preserved: %s", root.c_str());
            return false;
        }
        LOG_INF("DRVMGR", "Recovered matching interrupted driver download: %s", id);
    }
    if (!Storage.mkdir(root.c_str(), false) ||
        !writeExclusive(root + "/.package.json", descriptor)) return false;
    for (size_t i = 1; i < 4; ++i) {
        const std::string target = root + "/" + kNames[i];
        const std::string stage = target + ".part";
        if (HttpDownloader::downloadToFile(prefix + kNames[i], stage,
                [](size_t, size_t) { esp_task_wdt_reset(); }) != HttpDownloader::OK ||
            Storage.exists(target.c_str()) ||
            !Storage.rename(stage.c_str(), target.c_str()))
            return false;
    }
    constexpr PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 0,
                                          8u * 1024u * 1024u, 16u * 1024u * 1024u};
    Identity observed{};
    if (!verifyOrdinarySdDirectory(root.c_str(), policy,
                                   installedCapabilityVersion, observed) ||
        observed.kind != Kind::Driver || std::strcmp(observed.id, id)) return false;
    if (!Recovery::discardMatchingStage(root, *plan, policy, installedCapabilityVersion)) {
        LOG_ERR("DRVMGR", "Unknown interrupted driver stage preserved: %s", id);
        return false;
    }
    const auto result = installOrdinaryFromSd(root.c_str(), policy,
                                               installedCapabilityVersion);
    if (result.result != OrdinaryInstallResult::Installed) {
        LOG_ERR("DRVMGR", "Canonical driver install rejected for %s: result=%u stage=%u transaction=%u",
                id, static_cast<unsigned>(result.result),
                static_cast<unsigned>(result.staging), static_cast<unsigned>(result.transaction));
        return false;
    }
    // An interrupted download remains in the inbox for explicit inspection.
    // Only an exclusively created, successfully installed source is cleaned.
    (void)Storage.remove((root + "/.package.json").c_str());
    for (size_t i = 1; i < 4; ++i)
        (void)Storage.remove((root + "/" + kNames[i]).c_str());
    (void)Storage.rmdir(root.c_str());
    return true;
}
} // namespace DriverIntake
} // namespace RuntimeOnlinePackages
