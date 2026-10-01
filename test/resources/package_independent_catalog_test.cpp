#include "runtime/packages/PackageIndependentCatalog.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <utility>

using namespace RuntimePackages;

static const std::string sha(64, 'a');
static const std::string repo = "michaelrolphone-cmyk/T5S3-Reader";
static const std::string gameboyRepo = "michaelrolphone-cmyk/T5S3-GameBoy";

static std::string entry(const std::string& name, bool executable = false,
                         uint64_t size = 96) {
  return "{\"name\":\"" + name + "\",\"size_bytes\":" + std::to_string(size) +
         ",\"sha256\":\"" + sha + "\",\"executable\":" + (executable ? "true" : "false") + "}";
}
static std::string manifest(const std::string& id, const std::string& version,
                            const std::string& architecture = "xtensa-esp32s3") {
  return "{\"schema\":1,\"kind\":\"driver\",\"id\":\"" + id + "\",\"version\":\"" + version +
         "\",\"architecture\":\"" + architecture + "\",\"artifact\":\"driver.elf\","
         "\"min_runtime_api\":2,\"entries\":[" + entry("driver.elf", true) + "," +
         entry("provider-abi.v1") + "," + entry("privileged-imports.v1") +
         "],\"requires\":[{\"capability\":\"platform.clock\",\"min_api\":1}]}";
}
static std::string legacyManifest(const std::string& id, const std::string& version) {
  std::string files;
  for (const char* name : {".package.json", "driver.elf", "provider-abi.v1", "privileged-imports.v1"}) {
    if (!files.empty()) files += ",";
    files += "{\"name\":\"" + std::string(name) + "\",\"size_bytes\":12,\"sha256\":\"" + sha + "\"}";
  }
  return "{\"id\":\"" + id + "\",\"version\":\"" + version +
         "\",\"capability\":\"serial.port\",\"api\":1,\"files\":[" + files + "],"
         "\"description\":\"Legacy \\u4e2d\\u6587, \\n and \\\"quoted\\\" text\"}";
}
static std::string record(const std::string& id = "sample-module",
                          const std::string& version = "1.2.3", bool bundled = true,
                          const std::string& architecture = "xtensa-esp32s3") {
  const std::string tag = "driver-" + id + "-v" + version;
  const std::string asset = bundled ? "driver-" + id + "-" + version + "-" + architecture + ".rte.zip" : id + "--driver.elf";
  return "{\"kind\":\"driver\",\"id\":\"" + id + "\",\"version\":\"" + version + "\",\"tag\":\"" + tag +
         "\",\"asset\":\"" + asset + "\",\"url\":\"https://github.com/" + repo + "/releases/download/" + tag + "/" + asset +
         "\",\"size\":2048,\"sha256\":\"" + sha + "\",\"manifest\":" +
         (bundled ? manifest(id, version, architecture) : legacyManifest(id, version)) +
         (bundled ? ",\"format\":\"rte.zip\",\"architecture\":\"" + architecture + "\"" : "") + "}";
}
static std::string app(const std::string& id = "clock", const std::string& version = "1.2.3",
                       bool external = false, bool stamped = true) {
  const std::string tag = external ? "v1.3.1" : "app-" + id + "-v" + version;
  const std::string asset = id + ".elf";
  return "{\"kind\":\"app\",\"id\":\"" + id + "\",\"version\":\"" + version + "\",\"tag\":\"" + tag +
         "\",\"asset\":\"" + asset + "\",\"url\":\"https://github.com/" + (external ? gameboyRepo : repo) +
         "/releases/download/" + tag + "/" + asset + "\",\"size\":123,\"sha256\":\"" + sha + "\",\"manifest\":{" +
         "\"file_name\":\"" + asset + "\",\"version\":\"" + version + "\",\"display_name\":\"Clock 😀\"" +
         (stamped ? ",\"size_bytes\":123,\"sha256\":\"" + sha + "\"" : "") + "}" +
         (external ? ",\"source_repo\":\"" + gameboyRepo + "\"" : "") + "}";
}
static std::string bundledApp(const std::string& id = "clock", const std::string& version = "1.2.4") {
  const std::string tag = "app-" + id + "-v" + version;
  const std::string asset = "application-" + id + "-" + version + "-xtensa-esp32s3.rte.zip";
  const std::string ordinary = "{\"schema\":1,\"kind\":\"application\",\"id\":\""+id+
      "\",\"version\":\""+version+"\",\"architecture\":\"xtensa-esp32s3\",\"artifact\":\""+id+
      ".elf\",\"min_runtime_api\":2,\"entries\":["+entry(id+".elf",true)+","+entry(id+".json")+"],\"requires\":[]}";
  return "{\"kind\":\"app\",\"id\":\""+id+"\",\"version\":\""+version+"\",\"tag\":\""+tag+
      "\",\"asset\":\""+asset+"\",\"url\":\"https://github.com/"+repo+"/releases/download/"+tag+"/"+asset+
      "\",\"size\":2048,\"sha256\":\""+sha+"\",\"format\":\"rte.zip\",\"architecture\":\"xtensa-esp32s3\",\"manifest\":"+ordinary+"}";
}
static std::string index(const std::string& drivers = "", const std::string& apps = "",
                         const std::string& firmware = "null") {
  return "{\"schema\":1,\"firmware\":" + firmware + ",\"apps\":[" + apps + "],\"drivers\":[" + drivers + "]}";
}
static std::string replace(std::string src, const std::string& from, const std::string& to) {
  const size_t at = src.find(from);
  assert(at != std::string::npos);
  src.replace(at, from.size(), to);
  return src;
}
static bool accepted(const std::string& data, IndependentDriverCatalog& out) {
  return parseIndependentDriverCatalog(data.data(), data.size(), out);
}
static void reject(const std::string& data) {
  IndependentDriverCatalog out;
  out.schema = 99;
  out.rowCount = 13;
  out.appRowCount = 11;
  std::strcpy(out.rows[63].tag, "old-tag");
  std::strcpy(out.appRows[127].id, "old-app");
  assert(!accepted(data, out));
  assert(out.schema == 0 && out.rowCount == 0 && out.appRowCount == 0);
  for (const auto& row : out.rows)
    assert(!row.id[0] && !row.version[0] && !row.tag[0] && !row.bundled && !row.package.archive[0]);
  for (const auto& row : out.appRows) assert(!row.id[0] && !row.version[0]);
}
struct Checkpoints { size_t calls = 0; size_t cancelAt = 0; };
static bool checkpoint(void* context) {
  auto& progress = *static_cast<Checkpoints*>(context);
  ++progress.calls;
  return !progress.cancelAt || progress.calls != progress.cancelAt;
}

int main() {
  IndependentDriverCatalog out;
  assert(accepted(index(), out) && out.schema == 1 && !out.rowCount && !out.appRowCount);
  std::string valid = index(record() + "," + record("historical.id-", "1.10.0", false),
                            app() + "," + app("gameboy", "1.2.29", true));
  assert(accepted(valid, out) && out.rowCount == 2 && out.appRowCount == 2);
  assert(out.rows[0].bundled && out.rows[0].package.identity.kind == Kind::Driver);
  assert(std::string(out.rows[0].id) == "sample-module");
  assert(std::string(out.rows[0].package.identity.version) == "1.2.3");
  assert(std::string(out.rows[0].package.identity.artifact) == "driver.elf");
  assert(std::string(out.rows[0].tag) == "driver-sample-module-v1.2.3");
  assert(std::string(out.rows[0].package.archive) == "driver-sample-module-1.2.3-xtensa-esp32s3.rte.zip");
  assert(!out.rows[1].bundled && !out.rows[1].package.archive[0] && !out.rows[1].package.identity.id[0]);
  assert(std::string(out.rows[1].id) == "historical.id-" && std::string(out.rows[1].version) == "1.10.0");
  assert(std::string(out.appRows[1].version) == "1.2.29");
  assert(accepted(index(record("riscv", "0.0.0", true, "riscv32"), app("old", "1.0.0", false, false)), out));
  assert(accepted(index(record(std::string(63, 'a'), "4294967295.4294967295.429496729")), out));
  assert(std::strlen(out.rows[0].tag) > 64);
  assert(accepted(index(record(std::string(64, 'a'), "1.0.0", false)), out));

  reject(""); reject("{}"); reject("[]"); reject(valid + "garbage");
  reject(replace(valid, "\"schema\":1", "\"schema\":true"));
  reject(replace(valid, "\"schema\":1", "\"schema\":1.0"));
  reject(replace(valid, "\"schema\":1", "\"schema\":01"));
  reject(replace(valid, "\"schema\":1", "\"schema\":2"));
  reject(replace(valid, "\"schema\":1", "\"schema\":1,\"schema\":1"));
  reject(replace(valid, "\"schema\":1", "\"\\u0073chema\":1"));
  reject(replace(valid, "\"firmware\":null,", ""));
  reject(index("", "", "[]")); reject(index("", "null"));
  reject(index(record() + "," + record()));
  reject(index(record() + "," + record("sample-module", "2.0.0", false)));
  reject(index("", app() + "," + app()));
  reject(index(record("bad..id", "1.0.0", false)));
  reject(index(record("Bad-id", "1.0.0", false)));
  reject(index(record(std::string(64, 'a'))));
  reject(index(record("module", "01.2.3")));
  reject(index(record("module", "4294967296.2.3")));
  reject(index(record("module", "1.2.3-rc1")));

  const std::string row = record();
  for (const auto& change : {
      std::make_pair("\"id\":\"sample-module\"", "\"id\":\"sample\\u002dmodule\""),
      std::make_pair("\"id\":\"sample-module\"", "\"id\":\"sample-module\",\"id\":\"sample-module\""),
      std::make_pair("\"kind\":\"driver\"", "\"kind\":\"app\""),
      std::make_pair("\"format\":\"rte.zip\"", "\"format\":\"zip\""),
      std::make_pair("\"format\":\"rte.zip\"", "\"format\":null"),
      std::make_pair(",\"format\":\"rte.zip\"", ""),
      std::make_pair("\"size\":2048", "\"size\":0"),
      std::make_pair("\"size\":2048", "\"size\":21"),
      std::make_pair("\"size\":2048", "\"size\":4259841"),
      std::make_pair("\"size\":2048", "\"size\":18446744073709551616"),
      std::make_pair("\"size\":2048", "\"size\":2048.0"),
      std::make_pair("\"size\":2048", "\"size\":\"2048\""),
      std::make_pair("\"tag\":\"driver-sample-module-v1.2.3\"", "\"tag\":\"driver-sample-module-v1.2.4\""),
      std::make_pair("\"asset\":\"driver-sample-module-1.2.3-xtensa-esp32s3.rte.zip\"", "\"asset\":\"other.rte.zip\""),
      std::make_pair("https://github.com/", "http://github.com/"),
      std::make_pair("/releases/download/", "/releases/latest/download/"),
      std::make_pair("T5S3-Reader/releases", "other/releases"),
      std::make_pair("\"format\":\"rte.zip\"", "\"source_repo\":null,\"format\":\"rte.zip\""),
      std::make_pair("\"min_runtime_api\":2", "\"min_runtime_api\":0"),
      std::make_pair("\"size_bytes\":96", "\"size_bytes\":1048577"),
      std::make_pair("\"name\":\"provider-abi.v1\"", "\"name\":\"readme.txt\""),
      std::make_pair("\"name\":\"privileged-imports.v1\"", "\"name\":\"driver.elf\""),
      std::make_pair("\"artifact\":\"driver.elf\"", "\"artifact\":\"other.elf\""),
      std::make_pair("\"executable\":false", "\"executable\":true"),
      std::make_pair("\"min_api\":1", "\"min_api\":0")}) reject(index(replace(row, change.first, change.second)));
  reject(index(replace(row, sha, std::string(64, 'A'))));
  reject(index(replace(row, "\"manifest\":" + manifest("sample-module", "1.2.3"),
                           "\"manifest\":" + manifest("different", "1.2.3"))));
  reject(index(replace(row, "\"manifest\":" + manifest("sample-module", "1.2.3"),
                           "\"manifest\":" + manifest("sample-module", "2.0.0"))));
  reject(index(replace(row, "\"manifest\":" + manifest("sample-module", "1.2.3"),
                           "\"manifest\":" + manifest("sample-module", "1.2.3", "riscv32"))));
  assert(accepted(index(replace(row, "\"size\":2048", "\"size\":4259840")), out));
  // All three 1 MiB payloads fit; a fourth plus its descriptor does not.
  std::string large = row;
  while (large.find("\"size_bytes\":96") != std::string::npos)
    large = replace(large, "\"size_bytes\":96", "\"size_bytes\":1048576");
  assert(accepted(index(large), out));
  reject(index(replace(large, "],\"requires\":", "," + entry("resource.bin", false, 1048576) + "],\"requires\":")));
  reject(index(replace(row, "\"min_runtime_api\":2", "\"min_runtime_api\":2," + std::string(4100, ' '))));

  const std::string old = record("old", "1.2.3", false);
  reject(index(replace(old, "\"api\":1", "\"api\":false")));
  reject(index(replace(old, "\"api\":1", "\"api\":0")));
  reject(index(replace(old, "\"api\":1", "\"api\":1,\"schema\":1")));
  reject(index(replace(old, "\"name\":\".package.json\"", "\"name\":\"driver.elf\"")));
  reject(index(replace(old, "\"sha256\":\"" + sha + "\"", "\"sha256\":\"bad\"")));
  reject(index(replace(old, "\"size_bytes\":12", "\"size_bytes\":0")));
  reject(index(replace(old, "\"capability\":\"serial.port\"", "\"capability\":\"\"")));
  reject(index(replace(old, "\"api\":1", "\"api\":1,\"x\":{\"a\":1,\"a\":2}")));

  assert(accepted(index("", replace(app(), "\"display_name\":\"Clock 😀\"",
                                    "\"display_name\":\"" + std::string(5000, 'a') + "\"")), out));
  reject(index("", replace(app(), "\"display_name\":\"Clock 😀\"",
                            "\"display_name\":\"" + std::string(8192, 'a') + "\"")));
  reject(index("", app("other", "1.2.29", true)));
  reject(index("", replace(app(), "\"kind\":\"app\"", "\"kind\":\"driver\"")));
  reject(index("", replace(app(), "\"size_bytes\":123", "\"size_bytes\":124")));
  reject(index("", replace(app(), "\"file_name\":\"clock.elf\"", "\"file_name\":\"other.elf\"")));
  reject(index("", replace(app(), "\"kind\":\"app\"", "\"kind\":\"app\",\"format\":\"rte.zip\"")));
  reject(index("", replace(app(), "\"kind\":\"app\"", "\"kind\":\"app\",\"architecture\":\"riscv32\"")));
  reject(index("", replace(app("gameboy", "1.2.29", true), "\"source_repo\":\"" + gameboyRepo + "\"",
                                                                  "\"source_repo\":\"someone/GameBoy\"")));
  reject(index("", replace(app("gameboy", "1.2.29", true), "\"tag\":\"v1.3.1\"", "\"tag\":\"v01.3.1\"")));
  std::string appHash = app();
  const size_t lastHash = appHash.rfind(sha);
  assert(lastHash != std::string::npos);
  appHash.replace(lastHash, sha.size(), std::string(64, 'b'));
  reject(index("", appHash));

  // Ignored metadata still receives syntax, UTF-8, duplicate, depth and key bounds.
  assert(accepted(index(row, "", "{\"description\":\"\\uD83D\\uDE00 \\t \\\\ /\",\"other\":-0.5e+2}"), out));
  reject(index(row, "", "{\"x\":1,\"x\":2}"));
  reject(index(row, "", "{\"x\":\"\\uD800\"}"));
  reject(index(row, "", "{\"x\":\"\\uDC00\"}"));
  reject(index(row, "", "{\"x\":\"\\uD800\\u0041\"}"));
  reject(index(row, "", "{\"x\":\"\\q\"}"));
  reject(index(row, "", "{\"x\":01}"));
  reject(index(row, "", "{\"x\":1.}"));
  reject(index(row, "", "{\"x\":1e}"));
  reject(index(row, "", "{\"x\":\"" + std::string("\xc0\xaf", 2) + "\"}"));
  reject(index(row, "", "{\"x\":\"" + std::string("\xed\xa0\x80", 3) + "\"}"));
  reject(index(row, "", "{\"x\":\"" + std::string(1, '\0') + "\"}"));
  reject(index(row, "", "{\"x\":" + std::string(10, '[') + "0" + std::string(10, ']') + "}"));
  std::string keys;
  for (unsigned i = 0; i < 65; ++i) { if (i) keys += ","; keys += "\"k" + std::to_string(i) + "\":0"; }
  reject(index(row, "", "{" + keys + "}"));

  std::string drivers, apps;
  for (unsigned i = 0; i < 64; ++i) { if (i) drivers += ","; drivers += record("module-" + std::to_string(i)); }
  for (unsigned i = 0; i < 128; ++i) { if (i) apps += ","; apps += app("app-" + std::to_string(i)); }
  const std::string full = index(drivers, apps);
  assert(full.size() > 65536);
  assert(accepted(full, out) && out.rowCount == 64 && out.appRowCount == 128);
  reject(index(drivers + "," + record("overflow"), apps));
  reject(index(drivers, apps + "," + app("overflow")));

  const std::string shell = index("", "", "{\"description\":\"\"}");
  const std::string maximum = replace(shell, "\"description\":\"\"",
      "\"description\":\"" + std::string(kIndependentCatalogMaxBytes - shell.size(), 'a') + "\"");
  assert(maximum.size() == kIndependentCatalogMaxBytes && accepted(maximum, out));
  reject(maximum + " ");
  Checkpoints progress;
  assert(parseIndependentDriverCatalog(maximum.data(), maximum.size(), out, checkpoint, &progress));
  assert(progress.calls >= 2 * kIndependentCatalogMaxBytes / 1024);
  for (size_t when : {size_t(1), size_t(3), progress.calls}) {
    Checkpoints cancelled;
    cancelled.cancelAt = when;
    assert(!parseIndependentDriverCatalog(maximum.data(), maximum.size(), out, checkpoint, &cancelled));
    assert(!out.rowCount && !out.appRowCount && !out.schema);
  }
  Checkpoints fullProgress;
  assert(parseIndependentDriverCatalog(full.data(), full.size(), out, checkpoint, &fullProgress));
  Checkpoints late;
  late.cancelAt = fullProgress.calls;
  assert(!parseIndependentDriverCatalog(full.data(), full.size(), out, checkpoint, &late));
  assert(!out.rowCount && !out.appRowCount && !out.rows[63].id[0] && !out.appRows[127].id[0]);

  const auto appZip = bundledApp();
  assert(accepted(index(record(), appZip + "," + app("gameboy", "1.2.3", true)), out));
  assert(out.appRowCount == 2 && out.appRows[0].bundled && !out.appRows[1].bundled);
  assert(out.appRows[0].package.identity.kind == Kind::Application);
  assert(std::string(out.appRows[0].tag) == "app-clock-v1.2.4");
  IndependentAppRecord oneApp{}; OrdinaryPackagePlan scratch{};
  assert(parseIndependentAppRecord(appZip.data(), appZip.size(), oneApp, scratch) && oneApp.bundled);
  assert(parseIndependentAppRecord(app().data(), app().size(), oneApp, scratch) && !oneApp.bundled);
  reject(index("", replace(appZip, "\"artifact\":\"clock.elf\"", "\"artifact\":\"other.elf\"")));
  reject(index("", replace(appZip, "\"name\":\"clock.json\"", "\"name\":\"missing.json\"")));
  reject(index("", replace(appZip, "\"kind\":\"application\"", "\"kind\":\"driver\"")));
  reject(index("", replace(appZip, "\"format\":\"rte.zip\"", "\"format\":\"elf\"")));
  reject(index("", replace(appZip, "\"kind\":\"app\"", "\"kind\":\"app\",\"source_repo\":\""+gameboyRepo+"\"")));

  // Every truncation of a complete driver row must fail without residual data.
  const std::string one = index(row);
  for (size_t i = 0; i < one.size(); ++i) reject(one.substr(0, i));
  assert(!parseIndependentDriverCatalog(nullptr, 2, out));
  std::puts("Independent catalog: ZIP/legacy barriers, app lineage, immutable locators, strict bounds/JSON, cancellation and reset PASS");
}
