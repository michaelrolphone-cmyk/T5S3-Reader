#!/usr/bin/env python3
"""Compile actual Home selection methods for both boards; guard shared routes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class SharedHomeDispatch(unittest.TestCase):
    def test_real_home_selection_on_both_boards(self):
        source = (subprocess.check_output(['git', 'show', os.environ['HOME_SOURCE_REF'] + ':src/activities/home/HomeActivity.cpp'], cwd=ROOT, text=True)
                  if os.environ.get('HOME_SOURCE_REF') else
                  (ROOT / 'src/activities/home/HomeActivity.cpp').read_text())
        methods = source[source.index('void HomeActivity::onSelectBook('):]
        harness = r'''
#include <cassert>
#include <string>
#include <vector>
using std::size_t;
struct Manager {
  std::string destination;
  void goToReader(const std::string& p) { destination = p; }
  void goToRecentBooks() { destination = "recents"; }
  void goToSettings() { destination = "settings"; }
  void goToBrowser() { destination = "opds"; }
} activityManager;
struct HomeActivity {
  struct Book { std::string path; };
  struct App { std::string file_name; };
  std::vector<Book> recentBooks;
  std::vector<App> homeApps;
  bool hasOpdsServers = false, appsPending = false;
  std::string pendingHomeAppArtifact, x4Status;
  void requestUpdate() {}
  void freeCoverBuffer() {}
  void onSelectBook(const std::string&);
  void activateSelection(int);
  void onHomeAppOpen(size_t);
  void onRecentsOpen();
  void onSettingsOpen();
  void onOpdsBrowserOpen();
};
'''
        checks = r'''
int main() {
  HomeActivity home;
  home.activateSelection(0);
  assert(activityManager.destination == "recents");
  home.activateSelection(1);
  assert(home.appsPending);
  home.activateSelection(2);
  assert(activityManager.destination == "settings");
  home.appsPending = false;
  home.recentBooks = {{"/Books/book.epub"}};
  home.homeApps = {{"settings.elf"}};
  home.hasOpdsServers = true;
  home.activateSelection(0);
  assert(activityManager.destination == "/Books/book.epub");
  home.activateSelection(1);
  assert(activityManager.destination == "recents");
  home.activateSelection(2);
  assert(activityManager.destination == "opds");
  home.activateSelection(3);
  assert(home.appsPending);
  home.activateSelection(4);
  assert(home.pendingHomeAppArtifact == "settings.elf");
  home.activateSelection(5);
  assert(activityManager.destination == "settings");
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / 'home.cpp'
            cpp.write_text(harness + methods + checks)
            for board in ('BOARD_XTEINK_X4_PRO', 'BOARD_T5S3_PRO'):
                binary = Path(tmp) / board
                subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                                '-Werror', '-D' + board, str(cpp), '-o', str(binary)],
                               check=True, timeout=60)
                subprocess.run([str(binary)], check=True, timeout=10)

    def test_no_board_specific_home_or_recent_dispatch(self):
        for path in ('src/activities/home/HomeActivity.cpp', 'src/activities/home/HomeActivity.h'):
            self.assertNotIn('BOARD_XTEINK', (ROOT / path).read_text())
        manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
        method = manager.split('void ActivityManager::goToRecentBooks()', 1)[1].split('\n}', 1)[0]
        self.assertNotIn('#if', method)
        self.assertIn('std::make_unique<RecentBooksActivity>', method)
        self.assertFalse(list((ROOT / 'src/activities').rglob('X4Txt*')))

    def test_shared_boot_dependencies_and_app_input(self):
        boot = (ROOT / 'src/platform/X4DiagnosticBoot.cpp').read_text()
        for call in ('powerManager.begin()', 'setupDisplayAndFonts()', 'setupReaderState()',
                     'ButtonNavigator::setMappedInputManager(mappedInputManager)'):
            self.assertLess(boot.index(call), boot.index('activityManager.goHome()'))
        self.assertNotIn('insertFont(', boot)
        self.assertNotIn('openFirstText', boot)
        self.assertIn('mappedInputManager.update()', boot)
        inp = (ROOT / 'src/MappedInputManager.cpp').read_text()
        method = inp.split('void MappedInputManager::update() const {', 1)[1].split('\n}', 1)[0]
        self.assertIn('#if !defined(BOARD_XTEINK_X4_PRO)', method)
        self.assertLess(method.index('gpio.update()'), method.index('#endif'))
        self.assertLess(method.index('#endif'), method.index('nativeNavigationTick()'))
        self.assertLess(method.index('#endif'), method.index('nativeTouchTick()'))

if __name__ == '__main__':
    unittest.main()
