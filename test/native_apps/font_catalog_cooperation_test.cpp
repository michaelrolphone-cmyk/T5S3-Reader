// Provider wire/card setup and production definitions are extracted at build
// time by the adjacent runner. Only hardware, network, registry and OS endpoints
// are modeled here. No production parser, validator, CRC loop or app is copied.
#include "card.inc"
#include <ArduinoJson.h>
#include <HalReadBudget.h>
#include <T5AppApi.h>
#include <T5FontApi.h>
#include <T5UiApi.h>
#include <FontCatalogValidation.h>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>

unsigned hal_waits;
static const risc_storage_volume_api_v1* raw;
static const risc_storage_volume_api_v1_ext* rawext;
static std::map<uint32_t, std::string> paths;
static std::set<uint32_t> sticky;
static std::set<std::string> installed;
static std::vector<std::string> trace;
static bool recording, app_active = true;
static std::string downloaded;
static unsigned reads, read_bytes, accepted_bytes, manifest_reads, manifest_bytes;
static unsigned waits_start, sleeps_start, parser_waits, registry_refreshes;
static uint32_t time_start;
static unsigned sectors_start, writes_start;
static constexpr const char* temp_path = "/fonts_manifest.tmp";
struct Fault {
  size_t before = SIZE_MAX;
  size_t after_copy = SIZE_MAX;
  bool open = false, network = false, unavailable = false, media = false, font_read = false;
  unsigned close = 0, remove = 0, latency = 0;
  uint64_t start = 0;
};
static Fault fault;

template <typename... T> static void event(const T&... fields) {
  if (!recording) return;
  std::ostringstream out;
  ((out << fields << '|'), ...);
  trace.push_back(out.str());
}
static std::string hex(const void* bytes, size_t count) {
  static const char digits[] = "0123456789abcdef";
  const auto* p = static_cast<const uint8_t*>(bytes);
  std::string result;
  for (size_t i = 0; i < count; ++i) { result += digits[p[i] >> 4]; result += digits[p[i] & 15]; }
  return result;
}
static size_t countedRead(void* context, uint32_t handle, void* dst, size_t count) {
  uint64_t size = 0, before = 0, after = 0;
  assert(rawext->file_info(context, handle, &size, &before));
  const bool manifest = paths.at(handle) == temp_path;
  if (manifest) assert(count == 1 && "catalog must retain scalar requests without read-ahead");
  size_t got = 0;
  if ((manifest && before >= fault.before) || (!manifest && fault.font_read)) sticky.insert(handle);
  else {
    got = raw->file_read(context, handle, dst, count);
    if (manifest && before + got >= fault.after_copy) sticky.insert(handle);
  }
  assert(rawext->file_info(context, handle, &size, &after));
  ++reads; read_bytes += got;
  if (manifest) {
    ++manifest_reads; manifest_bytes += got;
    if (!sticky.count(handle)) accepted_bytes += got;
    card_time += fault.latency;
  }
  event("read", handle, paths.at(handle), count, before, after, got, hex(dst, got));
  return got;
}
static uint32_t countedError(void* context, uint32_t handle, bool directory) {
  const uint32_t result = sticky.count(handle) ? 1 : rawext->handle_error(context, handle, directory);
  event("error", handle, directory, result);
  return result;
}
static uint32_t countedOpen(void* context, const char* path, uint32_t flags) {
  const uint32_t handle = recording && fault.open && path == std::string(temp_path) ? 0 :
      rawext->file_open(context, path, flags);
  if (handle) { paths[handle] = path; sticky.erase(handle); }
  event("open", path, flags, handle);
  return handle;
}
static bool countedClose(void* context, uint32_t handle, bool commit) {
  const bool manifest = paths.at(handle) == temp_path;
  uint64_t size = 0, position = 0;
  assert(rawext->file_info(context, handle, &size, &position));
  bool ok;
  if (recording && manifest && fault.close) { --fault.close; ok = false; }
  else ok = raw->file_close(context, handle, commit);
  event("close", handle, paths.at(handle), position, commit, ok);
  if (recording && manifest) parser_waits = hal_waits - waits_start;
  if (ok) { paths.erase(handle); sticky.erase(handle); }
  return ok;
}
static bool countedRemove(void* context, const char* path) {
  bool ok;
  if (recording && fault.remove && path == std::string(temp_path)) { --fault.remove; ok = false; }
  else ok = raw->remove(context, path);
  event("remove", path, ok);
  return ok;
}
static bool countedReady(void* context) {
  return !(recording && fault.media) && raw->ready(context);
}
static void beginMeasurements() {
  recording = true; trace.clear(); reads = read_bytes = accepted_bytes = 0;
  manifest_reads = manifest_bytes = parser_waits = registry_refreshes = 0;
  waits_start = hal_waits; sleeps_start = sleeps;
  time_start = static_cast<uint32_t>(card_time); sectors_start = card_reads; writes_start = card_writes;
}
struct HttpDownloader {
  enum Result { OK, FAIL };
  static Result downloadToFile(const char* url, const char* path, void*) {
    // Supply completed network bytes through the real HAL/provider/FatFs.
    // Writing and SD download setup are excluded from parser scheduling costs.
    recording = false;
    auto file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    assert(file && file.write(downloaded.data(), downloaded.size()) == downloaded.size());
    assert(file.close());
    if (fault.start) card_time = ((card_time >> 32) + 1) * (uint64_t{1} << 32) + fault.start;
    beginMeasurements();
    event("download", url, path, downloaded.size(), fault.network);
    return fault.network ? FAIL : OK;
  }
};
#define FONT_MANIFEST_URL "fixture://completed-download"
#define FONTS_MANIFEST_VERSION 1
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
static void vTaskDelay(unsigned ticks) { assert(ticks == 1); ++hal_waits; ++card_time; }
// Deterministic ROM endpoint, with incremental CRC semantics. The bounded
// computeCrc32 production loop itself is extracted unchanged by the runner.
static uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* bytes, uint32_t count) {
  crc = ~crc;
  for (uint32_t i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
  }
  return ~crc;
}
struct FontInstaller {
  static bool isValidFamilyName(const char*);
  static bool isValidCpfontFilename(const char*);
  void refreshRegistry() { ++registry_refreshes; event("registry-refresh"); }
  bool isFamilyInstalled(const char* family) const {
    const bool yes = installed.count(family);
    event("registry-installed", family, yes); return yes;
  }
  static void buildFontPath(const char* family, const char* file, char* out, size_t capacity) {
    const int n = std::snprintf(out, capacity, "/fonts/%s/%s", family, file);
    assert(n >= 0 && static_cast<size_t>(n) < capacity);
    event("font-path", out);
  }
};
static bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }
static FontInstaller& installer() { static FontInstaller instance; return instance; }
#include "production_font.inc"

static std::string publishedState() {
  JsonDocument doc; doc["baseUrl"] = baseUrl;
  auto rows = doc["families"].to<JsonArray>();
  for (const auto& family : families) {
    auto row = rows.add<JsonObject>();
    row["name"] = family.name; row["description"] = family.description;
    row["size"] = family.totalSize; row["installed"] = family.installed; row["update"] = family.hasUpdate;
    auto files = row["files"].to<JsonArray>();
    for (const auto& file : family.files) {
      auto entry = files.add<JsonObject>();
      entry["name"] = file.name; entry["size"] = file.size; entry["crc32"] = file.crc32;
    }
  }
  std::string result; serializeJson(doc, result); return result;
}
extern "C" void app_main(void);
static unsigned polls, renders;
static bool appPoll(t5_app_input_t* input, uint32_t wait) {
  assert(wait == 50); ++polls;
  event("poll", wait, reads, read_bytes);
  std::memset(input, 0, sizeof(*input)); input->buttons = T5_APP_BUTTON_BACK; return true;
}
static void renderList(const t5_ui_chrome_t* chrome, const t5_ui_list_row_t* rows,
                       uint32_t count, int32_t selected) {
  assert(chrome && rows && selected == 0 && count == families.size()); ++renders;
  event("render", chrome->title, chrome->subtitle, chrome->status, chrome->back_label,
        chrome->confirm_label, chrome->previous_label, chrome->next_label, count, selected, reads, read_bytes);
  for (uint32_t i = 0; i < count; ++i) {
    t5_font_family_info_t info{}; assert(familyInfo(i, &info));
    assert(rows[i].title == std::string(info.name) && rows[i].subtitle == std::string(info.description));
    event("row", i, rows[i].title, rows[i].subtitle, rows[i].value, unsigned(rows[i].flags));
  }
}
static int32_t hitTest(int16_t, int16_t) { assert(false); return -1; }
static int32_t nextIndex(int32_t, uint32_t) { assert(false); return -1; }
static t5_font_result_t noInstall(uint32_t, t5_font_progress_callback_t, void*) { assert(false); return T5_FONT_UNAVAILABLE; }
static t5_font_result_t noDelete(uint32_t) { assert(false); return T5_FONT_UNAVAILABLE; }
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  static const auto api = [] { t5_app_api_v1 a{}; a.abi_version = T5_APP_ABI_VERSION;
    a.struct_size = sizeof(a); a.poll = appPoll; return a; }();
  return app_active && version == T5_APP_ABI_VERSION ? &api : nullptr;
}
extern "C" const t5_ui_api_v1* t5_ui_get_api(uint32_t version) {
  static const auto api = [] { t5_ui_api_v1 a{}; a.api_version = T5_UI_API_VERSION; a.struct_size = sizeof(a);
    a.render_list = renderList; a.hit_test = hitTest; a.next_index = nextIndex; a.previous_index = nextIndex; return a; }();
  return version == T5_UI_API_VERSION ? &api : nullptr;
}
extern "C" const t5_font_api_v1* t5_font_get_api(uint32_t version) {
  static const t5_font_api_v1 api = {T5_FONT_API_VERSION, sizeof(t5_font_api_v1), refreshCatalog,
    familyCount, familyInfo, noInstall, noDelete, nullptr, nullptr, nullptr};
  return version == T5_FONT_API_VERSION && active() ? &api : nullptr;
}
static std::string catalog(unsigned count, unsigned description = 0) {
  std::ostringstream out;
  out << "{\"version\":1,\"baseUrl\":\"https://example.invalid/fonts/\",\"families\":[";
  for (unsigned i = 0; i < count; ++i) {
    if (i) out << ',';
    out << "{\"name\":\"Family" << i << "\",\"description\":\"" << std::string(description, 'd') << "\",\"files\":[";
    for (unsigned j = 0; j < 4; ++j) {
      if (j) out << ',';
      out << "{\"name\":\"Family" << i << '_' << 12 + 2 * j << ".cpfont\",\"size\":4096,\"crc32\":12345678}";
    }
    out << "]}";
  }
  out << "]}"; return out.str();
}
static std::string replaced(std::string input, const std::string& needle, const std::string& replacement) {
  const auto at = input.find(needle); assert(at != std::string::npos);
  input.replace(at, needle.size(), replacement); return input;
}
static void run(const char* label, std::string bytes, t5_font_result_t expected,
                size_t expected_families, Fault settings = {}, bool launch = false) {
  fault = settings; downloaded = std::move(bytes); app_active = !fault.unavailable;
  const std::string prior = publishedState(); polls = renders = 0; beginMeasurements();
  t5_font_result_t result = T5_FONT_OK;
  if (launch) app_main(); else result = refreshCatalog();
  if (result != expected) std::fprintf(stderr, "%s result=%u expected=%u\n", label, result, expected);
  assert(result == expected && families.size() == expected_families);
  if (expected != T5_FONT_OK) assert(publishedState() == prior && "failure must preserve the entire prior catalog");
  if (launch) assert(polls == 1 && renders == 1);
  const unsigned waits = hal_waits - waits_start;
  const uint32_t elapsed = static_cast<uint32_t>(card_time) - time_start;
  // Real provider sleeps share millis with the HAL; elapsed checkpoints can
  // therefore yield sooner than the ideal 32-read bound. Preserve that effect.
  if (!COOPERATIVE_READER && manifest_reads && !settings.close)
    assert(parser_waits == accepted_bytes);
  if (COOPERATIVE_READER && accepted_bytes >= 32)
    assert(parser_waits >= accepted_bytes / 32);
  if (fault.latency && expected == T5_FONT_OK) {
    assert(parser_waits >= (COOPERATIVE_READER ? accepted_bytes / 3 : accepted_bytes));
    assert(elapsed == uint64_t(accepted_bytes) * fault.latency + waits + (sleeps - sleeps_start) * 10u);
  }
  JsonDocument report; report["case"] = label; report["result"] = unsigned(result);
  report["prior"] = prior; report["published"] = publishedState(); report["family_count"] = familyCount();
  auto infos = report["family_info"].to<JsonArray>();
  for (uint32_t i = 0; i < familyCount(); ++i) {
    t5_font_family_info_t info; std::memset(&info, 0xa5, sizeof(info));
    assert(familyInfo(i, &info));
    assert(info.name[sizeof(info.name) - 1] == 0 && info.description[sizeof(info.description) - 1] == 0);
    assert(info.total_size == families[i].totalSize && info.installed == families[i].installed && info.has_update == families[i].hasUpdate);
    assert(info.reserved[0] == 0 && info.reserved[1] == 0);
    infos.add(hex(&info, sizeof(info)));
  }
  t5_font_family_info_t untouched; std::memset(&untouched, 0xa5, sizeof(untouched));
  const auto saved = untouched;
  assert(!familyInfo(familyCount(), &untouched) && !std::memcmp(&saved, &untouched, sizeof(saved)));
  assert(!familyInfo(0, nullptr));
  report["read_calls"] = reads; report["read_bytes"] = read_bytes;
  report["manifest_calls"] = manifest_reads; report["manifest_bytes"] = manifest_bytes;
  report["registry_refreshes"] = registry_refreshes; report["renders"] = renders; report["polls"] = polls;
  report["schedule"]["waits"] = waits; report["schedule"]["parser_waits"] = parser_waits;
  report["schedule"]["elapsed"] = elapsed;
  report["schedule"]["provider_sleeps"] = sleeps - sleeps_start;
  report["sector_reads"] = card_reads - sectors_start; report["sector_writes"] = card_writes - writes_start;
  // Capture failed cleanup before an explicit bounded retry. A failed close is
  // retried by the real HalFile destructor, retaining the provider slot until it
  // succeeds; a failed remove leaves the downloaded file for this retry.
  fault.media = false;
  const bool remains = Storage.exists(temp_path);
  report["temp_remains"] = remains;
  assert(!fault.close && !fault.remove);
  if (settings.close || settings.remove || settings.media) assert(remains);
  if (remains) { assert(Storage.remove(temp_path)); event("cleanup-retry-complete"); }
  assert(!Storage.exists(temp_path) && paths.empty());
  assert(Storage.reconcileExternalStorage());
  auto events = report["trace"].to<JsonArray>(); for (const auto& e : trace) events.add(e);
  std::string output; serializeJson(report, output); std::puts(output.c_str());
  recording = false; fault = {}; app_active = true;
}

static uint32_t ideal_time;
static unsigned ideal_waits;
static uint32_t idealClock() { return ideal_time; }
static void idealYield() { ++ideal_waits; ++ideal_time; }
static void budgetUnitTests() {
  HalReadBudget ideal(idealClock, idealYield);
  for (unsigned i = 0; i < 26259; ++i) ideal.afterRead(1);
  assert(ideal_waits == 26259 / 32);
  ideal_time = UINT32_MAX - 4u; ideal_waits = 0;
  HalReadBudget wrap(idealClock, idealYield);
  ideal_time += 7; wrap.checkpoint(); assert(!ideal_waits);
  ++ideal_time; wrap.checkpoint(); assert(ideal_waits == 1);
  wrap.checkpoint(); assert(ideal_waits == 1);
}

int main() {
  budgetUnitTests();
  static_assert(!std::is_base_of<Stream, HalFile>::value, "exercise ArduinoJson's generic reader");
  static_assert(ARDUINOJSON_VERSION_MAJOR == 7 && ARDUINOJSON_VERSION_MINOR == 4 && ARDUINOJSON_VERSION_REVISION == 2);
  assert(esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926u);
  card_image = static_cast<uint8_t*>(std::calloc(card_sectors, 512)); assert(card_image); format(false);
  const auto* driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
  raw = static_cast<const risc_storage_volume_api_v1*>(driver->capability);
  rawext = risc_storage_volume_extension(raw); assert(rawext);
  risc_platform_clock_api_v1 clock = {1, sizeof(clock), nullptr, now, sleep};
  risc_provider_dependency_v1 deps[] = {{"platform.clock", 1, &clock}
#ifdef TEST_SPI_TRANSPORT
    , {"spi.bus", 1, &SpiCardFixture::api}
#endif
  };
  assert(driver->start(deps, sizeof(deps) / sizeof(deps[0])) && raw->ready(nullptr));
  auto hooked = *rawext; hooked.base.struct_size = sizeof(hooked);
  hooked.base.file_read = countedRead; hooked.handle_error = countedError; hooked.file_open = countedOpen;
  hooked.base.file_close = countedClose; hooked.base.remove = countedRemove; hooked.base.ready = countedReady;
  assert(Storage.bindVolume(&hooked.base));
  run("normal-1", catalog(1), T5_FONT_OK, 1);
  run("normal-16", catalog(16), T5_FONT_OK, 16);
  run("normal-64", catalog(64), T5_FONT_OK, 64);
  run("normal-64-desc127", catalog(64, 127), T5_FONT_OK, 64);
  run("app-launch-64-desc127", catalog(64, 127), T5_FONT_OK, 64, {}, true);
  for (const auto& tail : {std::string(4096, ' '), std::string("garbage"), std::string("{\"another\":1}"), std::string(1, '\0')}) {
    static unsigned serial; const auto label = "valid-root-tail-" + std::to_string(++serial);
    Fault f; f.before = catalog(1).size();
    run(label.c_str(), catalog(1) + tail, T5_FONT_OK, 1, f);
    assert(manifest_bytes == catalog(1).size() && "root-tail read error must remain unseen");
  }
  run("utf8-description", replaced(catalog(1), "\"description\":\"\"", "\"description\":\"caf\xc3\xa9 \xe6\x96\x87\""), T5_FONT_OK, 1);
  run("escaped-description", replaced(catalog(1), "\"description\":\"\"", "\"description\":\"line\\n\\u03a9\\\"\\\\\""), T5_FONT_OK, 1);
  run("escaped-nul-description", replaced(catalog(1), "\"description\":\"\"", "\"description\":\"a\\u0000b\""), T5_FONT_OK, 1);
  run("truncated-root", catalog(1).substr(0, catalog(1).size() - 1), T5_FONT_MANIFEST_ERROR, 1);
  run("empty-input", "", T5_FONT_MANIFEST_ERROR, 1);
  run("malformed-json", "{oops}", T5_FONT_MANIFEST_ERROR, 1);
  run("nul-mid-root", replaced(catalog(1), "Family0", std::string("Fa\0ily0", 7)), T5_FONT_MANIFEST_ERROR, 1);
  run("missing-families", "{\"version\":1}", T5_FONT_MANIFEST_ERROR, 1);
  run("wrong-families-type", "{\"version\":1,\"families\":{}}", T5_FONT_MANIFEST_ERROR, 1);
  run("wrong-version", replaced(catalog(1), "\"version\":1", "\"version\":2"), T5_FONT_MANIFEST_ERROR, 1);
  run("bad-family", replaced(catalog(1), "Family0\"", "../bad\""), T5_FONT_MANIFEST_ERROR, 1);
  run("bad-filename", replaced(catalog(1), "Family0_12.cpfont", "../bad.cpfont"), T5_FONT_MANIFEST_ERROR, 1);
  run("wrong-extension", replaced(catalog(1), "Family0_12.cpfont", "Family0_12.CPFONT"), T5_FONT_MANIFEST_ERROR, 1);
  run("crc-wrong-type", replaced(catalog(1), "12345678", "\"bad\""), T5_FONT_MANIFEST_ERROR, 1);
  run("crc-negative", replaced(catalog(1), "12345678", "-1"), T5_FONT_MANIFEST_ERROR, 1);
  run("crc-overflow", replaced(catalog(1), "12345678", "4294967296"), T5_FONT_MANIFEST_ERROR, 1);
  run("duplicate-family", replaced(catalog(2), "Family1\"", "family0\""), T5_FONT_MANIFEST_ERROR, 1);
  run("later-invalid-candidate", replaced(catalog(2), "Family1\"", "invalid/name\""), T5_FONT_MANIFEST_ERROR, 1);
  run("empty-files", "{\"version\":1,\"baseUrl\":\"discard\",\"families\":[{\"name\":\"Valid\",\"files\":[]}]}", T5_FONT_MANIFEST_ERROR, 1);
  run("files-wrong-type", "{\"version\":1,\"families\":[{\"name\":\"Valid\",\"files\":{}}]}", T5_FONT_MANIFEST_ERROR, 1);
  Fault f; f.before = 0; run("read-error-before-first", catalog(1), T5_FONT_MANIFEST_ERROR, 1, f);
  f.before = 1024; run("read-error-before-1024", catalog(16), T5_FONT_MANIFEST_ERROR, 1, f);
  f = {}; f.after_copy = catalog(1).size();
  run("copy-final-brace-sticky-error", catalog(1), T5_FONT_MANIFEST_ERROR, 1, f);
  assert(manifest_bytes == catalog(1).size() && accepted_bytes + 1 == manifest_bytes);
  f = {}; f.open = true; run("open-error", catalog(1), T5_FONT_STORAGE_ERROR, 1, f);
  f = {}; f.network = true; run("network-error", catalog(1), T5_FONT_NETWORK_ERROR, 1, f);
  f = {}; f.unavailable = true; run("app-unavailable", catalog(1), T5_FONT_UNAVAILABLE, 1, f);
  f = {}; f.media = true; run("media-unavailable", catalog(1), T5_FONT_STORAGE_ERROR, 1, f);
  f = {}; f.close = 1; run("close-failure-destructor-retry", catalog(1), T5_FONT_OK, 1, f);
  f = {}; f.remove = 1; run("remove-failure-explicit-retry", catalog(1), T5_FONT_OK, 1, f);
  run("valid-retry", catalog(16), T5_FONT_OK, 16);
  run("valid-empty-catalog", "{\"version\":1,\"baseUrl\":\"empty\",\"families\":[]}", T5_FONT_OK, 0);
  f = {}; f.latency = 3; run("elapsed-budget", catalog(1), T5_FONT_OK, 1, f);
  f.start = UINT32_MAX - 4u; run("elapsed-budget-wraparound", catalog(1), T5_FONT_OK, 1, f);
  f = {}; f.latency = 8; f.before = 0;
  run("elapsed-error-no-progress", catalog(1), T5_FONT_MANIFEST_ERROR, 1, f);
  assert(parser_waits == (COOPERATIVE_READER ? 1u : 0u));
  // Real installed-file open/size/hash branches, with a deterministic registry
  // endpoint. Files are created through the same actual SD/FatFs/HAL path.
  assert(Storage.mkdir("/fonts/Family0")); installed.insert("Family0");
  const std::string content(4096, 'F');
  auto font = Storage.open("/fonts/Family0/Family0_12.cpfont", O_WRONLY | O_CREAT | O_TRUNC);
  assert(font && font.write(content.data(), content.size()) == content.size() && font.close());
  const uint32_t crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(content.data()), content.size());
  const std::string one = "{\"version\":1,\"baseUrl\":\"installed\",\"families\":[{\"name\":\"Family0\",\"files\":[{\"name\":\"Family0_12.cpfont\",\"size\":4096,\"crc32\":" + std::to_string(crc) + "}]}]}";
  run("installed-crc-match", one, T5_FONT_OK, 1); assert(families[0].installed && !families[0].hasUpdate);
  run("installed-crc-mismatch", replaced(one, std::to_string(crc), std::to_string(crc ^ 1)), T5_FONT_OK, 1); assert(families[0].hasUpdate);
  f = {}; f.font_read = true;
  run("installed-crc-read-error", one, T5_FONT_OK, 1, f); assert(families[0].hasUpdate);
  run("installed-size-mismatch", replaced(one, "4096", "4095"), T5_FONT_OK, 1); assert(families[0].hasUpdate);
  run("installed-file-missing", replaced(one, "Family0_12.cpfont", "Missing.cpfont"), T5_FONT_OK, 1); assert(families[0].hasUpdate);
  assert(driver->quiesce()); driver->stop();
  assert(sd_mutex_creates == sd_mutex_deletes && !card_bad_pin);
  std::free(card_image);
}
