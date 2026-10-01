// Link the real C app and C++ bridge. Only persistence, polling and rendering
// are substitutes; physical events are translated using the LIVE settings.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "T5AppApi.h"
#include "T5ButtonRemapApi.h"
#include "T5UiApi.h"

extern "C" void app_main(void);

using Mapping = t5_button_remap_mapping_t;
static const Mapping defaults{{0, 1, 2, 3}};
static const Mapping custom{{2, 3, 1, 0}};
static CrossPointSettings settings;
static Mapping persisted;
static Mapping original;
static unsigned fail_saves;
static std::vector<Mapping> writes;
static const t5_button_remap_api_v1* bridge;

static Mapping live() {
  return {{settings.frontButtonBack, settings.frontButtonConfirm,
           settings.frontButtonLeft, settings.frontButtonRight}};
}

static void equal(const Mapping& actual, const Mapping& expected) {
  assert(std::memcmp(actual.role_to_hardware, expected.role_to_hardware,
                     T5_BUTTON_REMAP_ROLE_COUNT) == 0);
}

static void expect_state(const Mapping& expected) {
  equal(live(), expected);  // Check the actual input-facing fields, not a cache.
  Mapping read{};
  assert(bridge->read_mapping(&read));
  equal(read, expected);
  equal(persisted, expected);
}

CrossPointSettings& CrossPointSettings::getInstance() { return settings; }

bool CrossPointSettings::saveToFile() const {
  writes.push_back(live());
  if (fail_saves) {
    --fail_saves;
    return false;  // Model failed atomic storage without replacing old data.
  }
  persisted = live();
  return true;
}

static void initialize(const Mapping& mapping, unsigned failures = 0) {
  settings.frontButtonBack = mapping.role_to_hardware[0];
  settings.frontButtonConfirm = mapping.role_to_hardware[1];
  settings.frontButtonLeft = mapping.role_to_hardware[2];
  settings.frontButtonRight = mapping.role_to_hardware[3];
  original = persisted = mapping;
  fail_saves = failures;
  writes.clear();
}

static std::vector<Mapping> permutations() {
  std::vector<Mapping> result;
  Mapping mapping = defaults;
  do {
    result.push_back(mapping);
  } while (std::next_permutation(mapping.role_to_hardware,
                                 mapping.role_to_hardware + T5_BUTTON_REMAP_ROLE_COUNT));
  assert(result.size() == 24);
  return result;
}

static void test_bridge() {
  assert(t5_button_remap_get_api(0) == nullptr);
  assert(t5_button_remap_get_api(T5_BUTTON_REMAP_API_VERSION + 1) == nullptr);
  assert(bridge->api_version == T5_BUTTON_REMAP_API_VERSION);
  assert(bridge->struct_size == sizeof(*bridge));
  initialize(custom);
  assert(!bridge->read_mapping(nullptr));
  assert(!bridge->apply_mapping(nullptr));
  assert(writes.empty());
  expect_state(custom);

  // All 256 four-button tuples in the hardware domain: exactly the 24
  // permutations are valid. Invalid requests may neither mutate nor save.
  unsigned valid_count = 0;
  for (unsigned tuple = 0; tuple < 256; ++tuple) {
    Mapping candidate{};
    unsigned bits = tuple;
    unsigned seen = 0;
    for (auto& hardware : candidate.role_to_hardware) {
      hardware = bits & 3u;
      bits >>= 2;
      seen |= 1u << hardware;
    }
    initialize(custom);
    const bool valid = seen == 15;
    assert(bridge->apply_mapping(&candidate) == valid);
    assert(writes.size() == (valid ? 1u : 0u));
    expect_state(valid ? candidate : custom);
    valid_count += valid;
  }
  assert(valid_count == 24);
  for (unsigned role = 0; role < 4; ++role) {
    for (unsigned value = 4; value <= 255; ++value) {
      initialize(custom);
      Mapping invalid = defaults;
      invalid.role_to_hardware[role] = static_cast<uint8_t>(value);
      assert(!bridge->apply_mapping(&invalid));
      assert(writes.empty());
      expect_state(custom);
    }
  }

  for (const auto& before : permutations()) {
    for (const auto& target : permutations()) {
      initialize(before, 2);
      for (unsigned attempt = 0; attempt < 2; ++attempt) {
        assert(!bridge->apply_mapping(&target));
        expect_state(before);
        equal(writes.back(), target);
      }
      assert(bridge->apply_mapping(&target));
      expect_state(target);
      assert(writes.size() == 3);
    }
    initialize(before, 1);
    assert(!bridge->reset_defaults());
    equal(writes.back(), defaults);
    expect_state(before);
    assert(bridge->reset_defaults());
    expect_state(defaults);
    assert(writes.size() == 2);
  }
  std::puts("Button Remap bridge PASS: validation, 576 mapping transitions, repeated failed apply/reset rollback and retry");
}

enum class Input { Physical, Up, Down, Tap, Exit, PollFailure };
struct Event { Input kind; int value = 0; };
struct Frame {
  int32_t selected;
  std::string status;
  std::array<std::string, 4> values;
};
static std::vector<Event> events;
static size_t next_event;
static unsigned back_changes;
static bool back_exits;
static bool check_chrome;
static std::vector<Frame> frames;

static bool poll(t5_app_input_t* input, uint32_t wait_ms) {
  assert(input && wait_ms == 50);
  assert(!back_exits);
  assert(next_event < events.size());  // Fail promptly instead of spinning.
  *input = {};
  const auto event = events[next_event++];
  switch (event.kind) {
    case Input::Physical: {
      const Mapping active = live();
      const uint32_t roles[] = {T5_APP_BUTTON_BACK, T5_APP_BUTTON_CONFIRM,
                                T5_APP_BUTTON_LEFT, T5_APP_BUTTON_RIGHT};
      for (unsigned role = 0; role < 4; ++role) {
        if (active.role_to_hardware[role] == event.value) {
          input->buttons = roles[role];
          return true;
        }
      }
      assert(false);
      break;
    }
    case Input::Up: input->buttons = T5_APP_BUTTON_UP; break;
    case Input::Down: input->buttons = T5_APP_BUTTON_DOWN; break;
    case Input::Tap:
      input->tapped = true;
      input->touch_y = static_cast<int16_t>(event.value);
      break;
    case Input::Exit: input->exit_requested = true; break;
    case Input::PollFailure: return false;
  }
  return true;
}

static void set_back_exits(bool enabled) {
  ++back_changes;
  assert(enabled == (back_changes == 2));
  back_exits = enabled;
}

static void render(const t5_ui_chrome_t* chrome, const t5_ui_list_row_t* rows,
                   uint32_t count, int32_t selected) {
  assert(chrome && rows && count == 4 && selected >= 0 && selected < 4);
  assert(std::strcmp(chrome->title, "Remap Front Buttons") == 0);
  if (check_chrome) {
    assert(std::strcmp(chrome->subtitle, "Side Up: Reset | Side Down: Cancel") == 0);
    assert(chrome->back_label && chrome->back_label[0] == '\0');
    assert(chrome->confirm_label && chrome->confirm_label[0] == '\0');
    assert(chrome->previous_label && chrome->previous_label[0] == '\0');
    assert(chrome->next_label && chrome->next_label[0] == '\0');
  }
  Frame frame{selected, chrome->status, {}};
  const char* names[] = {"Back", "Confirm", "Left", "Right"};
  for (unsigned i = 0; i < 4; ++i) {
    assert(std::strcmp(rows[i].title, names[i]) == 0);
    frame.values[i] = rows[i].value;
  }
  frames.push_back(frame);
  if (frame.status == "Could not save button mapping" || frame.status == "Could not save defaults") {
    expect_state(original);  // Failure must already have rolled back at render.
  }
}

static int32_t hit_test(int16_t, int16_t y) { return y; }

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  static const t5_app_api_v1 api = [] {
    t5_app_api_v1 result{};
    result.abi_version = T5_APP_ABI_VERSION;
    result.struct_size = sizeof(result);
    result.poll = poll;
    result.set_back_exits_app = set_back_exits;
    return result;
  }();
  return version == T5_APP_ABI_VERSION ? &api : nullptr;
}

extern "C" const t5_ui_api_v1* t5_ui_get_api(uint32_t version) {
  static const t5_ui_api_v1 api = [] {
    t5_ui_api_v1 result{};
    result.api_version = T5_UI_API_VERSION;
    result.struct_size = sizeof(result);
    result.render_list = render;
    result.hit_test = hit_test;
    return result;
  }();
  return version == T5_UI_API_VERSION ? &api : nullptr;
}

static void launch(std::vector<Event> script) {
  events = std::move(script);
  next_event = back_changes = 0;
  back_exits = true;
  frames.clear();
  app_main();
  assert(next_event == events.size());
  assert(back_changes == 2 && back_exits);
  assert(!frames.empty());
  assert(frames.front().selected == 0);
  assert(frames.front().status == "Press a front button for the selected role");
  for (const auto& value : frames.front().values) assert(value == "Unassigned");
}

static std::vector<Event> buttons(const Mapping& target, unsigned count = 4) {
  std::vector<Event> script;
  for (unsigned i = 0; i < count; ++i) script.push_back({Input::Physical, target.role_to_hardware[i]});
  return script;
}

static void test_app() {
  for (const auto& before : permutations()) {
    for (const auto& target : permutations()) {
      initialize(before, 2);
      auto script = buttons(target);
      script.push_back({Input::Physical, target.role_to_hardware[3]});
      script.push_back({Input::Physical, target.role_to_hardware[3]});
      launch(script);
      assert(writes.size() == 3 && frames.size() == 6);
      for (const auto& write : writes) equal(write, target);
      for (unsigned i = 4; i < 6; ++i) {
        assert(frames[i].selected == 3);
        assert(frames[i].status == "Could not save button mapping");
        for (unsigned role = 0; role < 4; ++role) {
          assert(frames[i].values[role] == "Button " + std::to_string(target.role_to_hardware[role] + 1));
        }
      }
      expect_state(target);
    }

    for (unsigned assigned = 0; assigned < 4; ++assigned) {
      initialize(before);
      auto script = buttons(custom, assigned);
      script.push_back({Input::Down});
      launch(script);
      assert(writes.empty());
      expect_state(before);
    }

    initialize(before, 1);
    auto script = buttons(custom);
    script.push_back({Input::Down});
    launch(script);
    assert(writes.size() == 1);
    assert(frames.back().status == "Could not save button mapping");
    expect_state(before);

    // Reset reports failure and exits today. Reopening and retrying must still
    // read the pre-failure mapping and persist the defaults only on success.
    initialize(before, 1);
    launch({{Input::Physical, 3}, {Input::Up}});
    assert(writes.size() == 1);
    assert(frames.back().status == "Could not save defaults");
    expect_state(before);
    launch({{Input::Up}});
    assert(writes.size() == 2);
    assert(frames.back().status == "Default mapping restored");
    expect_state(defaults);
  }

  initialize(custom);
  launch({{Input::Physical, 0}, {Input::Physical, 0}, {Input::Down}});
  assert(writes.empty());
  assert(frames.back().selected == 1);
  assert(frames.back().status == "Button 1 is already assigned");
  assert(frames.back().values[1] == "Unassigned");
  expect_state(custom);

  initialize(custom);
  launch({{Input::Tap, 2}, {Input::Physical, 0}, {Input::Down}});
  assert(writes.empty());
  assert(frames[1].selected == 2 && frames[2].values[2] == "Button 1");
  expect_state(custom);

  for (const auto stop : {Input::Exit, Input::PollFailure}) {
    initialize(custom);
    launch({{Input::Physical, 0}, {stop}});
    assert(writes.empty());
    expect_state(custom);
  }
  std::puts("Button Remap app PASS: 576 live-input retry transitions, reset failure/reopen, cancel, duplicate, touch, exit cleanup");
}

static void test_chrome() {
  check_chrome = true;
  initialize(custom);
  launch({{Input::Down}});
  assert(writes.empty());
  expect_state(custom);
  check_chrome = false;
  std::puts("Button Remap chrome PASS: side-button instructions and blank front-button hints");
}

int main(int argc, char** argv) {
  assert(argc == 2);
  bridge = t5_button_remap_get_api(T5_BUTTON_REMAP_API_VERSION);
  assert(bridge);
  const std::string selected = argv[1];
  assert(selected == "all" || selected == "bridge" || selected == "app" || selected == "chrome");
  if (selected == "all" || selected == "bridge") test_bridge();
  if (selected == "all" || selected == "app") test_app();
  if (selected == "all" || selected == "chrome") test_chrome();
  return 0;
}
