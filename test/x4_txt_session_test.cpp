#include "activities/home/X4TxtSession.h"

#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct FakeSource final : X4TxtSession::Source {
  bool ready = true;
  bool listFails = false;
  bool openFails = false;
  bool readFails = false;
  bool closeFails = false;
  bool opened = false;
  int opens = 0;
  int closes = 0;
  std::string contents;
  size_t reportedBytes = 0;
  std::map<std::string, std::vector<std::string>> directories;

  bool available() const override { return ready; }
  bool list(const char* directory, std::vector<std::string>& names) override {
    if (listFails || !ready) return false;
    names = directories[directory];
    return true;
  }
  bool open(const std::string& path, size_t& bytes) override {
    if (openFails || opened || !ready || path.find(".TXT") == std::string::npos) return false;
    opened = true;
    ++opens;
    bytes = reportedBytes ? reportedBytes : contents.size();
    return true;
  }
  bool readAt(size_t offset, uint8_t* output, size_t count) override {
    if (readFails || !opened || !ready || offset > contents.size() || count > contents.size() - offset)
      return false;
    memcpy(output, contents.data() + offset, count);
    return true;
  }
  bool close() override {
    if (!opened) return true;
    ++closes;
    if (closeFails) return false;
    opened = false;
    return true;
  }
};

int main() {
  using Action = X4TxtSession::Action;
  using View = X4TxtSession::View;
  FakeSource source;
  X4TxtSession session;
  source.ready = false;
  session.enter(source);
  assert(session.status() == "SD unavailable" && session.files().empty());
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Home);

  source.ready = true;
  source.directories["/Books"] = {"B.TXT", "A.TXT", "SKIP.EPUB", "../X.TXT", "TOOLONG99.TXT"};
  source.directories["/"] = {"ROOT.TXT"};
  source.contents = std::string(330, 'x');
  source.contents[3] = '\n';
  session.enter(source);
  assert(session.files().size() == 3);
  assert(session.files()[0] == "/Books/A.TXT");
  assert(session.files()[1] == "/Books/B.TXT");
  assert(session.files()[2] == "/ROOT.TXT");
  assert(session.input(RISC_NAV_LEFT | RISC_NAV_RIGHT, 0, source) == Action::None);
  assert(session.selection() == 0);
  assert(session.input(RISC_NAV_LEFT, 0, source) == Action::Redraw && session.selection() == 3);
  assert(session.input(RISC_NAV_RIGHT, 0, source) == Action::Redraw && session.selection() == 0);
  assert(session.input(RISC_NAV_RIGHT, 0, source) == Action::Redraw && session.selection() == 1);
  assert(session.input(RISC_NAV_CONFIRM, 0, source) == Action::None && source.opens == 0);
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw);
  assert(session.view() == View::Page && source.opens == 1 && session.page().size() == 160);
  assert(session.page()[3] == ' ');  // Control characters never reach the screen.
  assert(session.input(RISC_NAV_RIGHT, 0, source) == Action::Redraw && session.offset() == 160);
  assert(session.input(RISC_NAV_RIGHT, 0, source) == Action::Redraw && session.offset() == 320);
  assert(session.page().size() == 10);
  assert(session.input(RISC_NAV_RIGHT, 0, source) == Action::None && session.offset() == 320);
  assert(session.input(RISC_NAV_LEFT, 0, source) == Action::Redraw && session.offset() == 160);
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw);
  assert(session.view() == View::List && source.closes == 1 && !source.opened);
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw && source.opens == 2);
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw && source.closes == 2);

  source.openFails = true;
  assert(session.input(RISC_NAV_RIGHT, 0, source) == Action::Redraw && session.selection() == 2);
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw);
  assert(session.view() == View::List && session.status() == "TXT unavailable" && source.opens == 2);
  source.openFails = false;
  source.readFails = true;
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw);
  assert(session.view() == View::List && session.status() == "TXT read failed");
  assert(!source.opened);
  source.readFails = false;
  session.leave(source);
  session.enter(source);
  assert(session.selection() == 0 && session.files().size() == 3);
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Home);

  source.reportedBytes = X4TxtSession::kMaxBytes + 1;
  assert(session.input(RISC_NAV_RIGHT, 0, source) == Action::Redraw);
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw);
  assert(session.view() == View::List && session.status() == "TXT unavailable" && !source.opened);
  source.reportedBytes = 0;
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw);
  source.closeFails = true;
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw);
  assert(session.view() == View::List && session.status() == "TXT close failed" && source.opened);
  assert(session.input(0, RISC_NAV_CONFIRM, source) == Action::Redraw);
  assert(session.status() == "TXT unavailable" && source.opened);
  source.closeFails = false;
  session.leave(source);
  assert(!source.opened);
  session.enter(source);
  assert(session.selection() == 0 && session.files().size() == 3);

  source.directories.clear();
  session.enter(source);
  assert(session.status() == "No TXT files on SD" && session.files().empty());
  source.listFails = true;
  session.enter(source);
  assert(session.status() == "SD listing failed" && session.files().empty());
}
