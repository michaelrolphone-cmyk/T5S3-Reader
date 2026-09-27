#!/usr/bin/env python3
"""Keep Home pinned ELF launches on the same deferred handoff as other launchers."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/home/HomeActivity.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/activities/home/HomeActivity.h").read_text(encoding="utf-8")
HOST = (ROOT / "src/native/NativeAppHost.cpp").read_text(encoding="utf-8")
RESOLVER = (ROOT / "src/native/InstalledAppPath.cpp").read_text(encoding="utf-8")
BASE_THEME = (ROOT / "src/components/themes/BaseTheme.cpp").read_text(encoding="utf-8")
LYRA_THEME = (ROOT / "src/components/themes/lyra/LyraThemeDrawD.cpp").read_text(encoding="utf-8")
ROUNDED_THEME = (ROOT / "src/components/themes/roundedraff/RoundedRaffTheme.cpp").read_text(encoding="utf-8")


class HomeShortcutLaunchContract(unittest.TestCase):
    def test_home_shortcut_launch_is_deferred_out_of_input_dispatch(self):
        self.assertIn("std::string pendingHomeAppArtifact;", HEADER)

        open_start = SOURCE.index("void HomeActivity::onHomeAppOpen(size_t index)")
        open_end = SOURCE.index("\nvoid HomeActivity::onRecentsOpen()", open_start)
        open_block = SOURCE[open_start:open_end]
        self.assertIn(
            "pendingHomeAppArtifact = homeApps[index].file_name;",
            open_block,
        )
        self.assertNotIn("runNativeApp(", open_block)
        self.assertNotIn("resolveInstalledAppPath(", open_block)

        loop_start = SOURCE.index("void HomeActivity::loop()")
        touch_start = SOURCE.index("\nbool HomeActivity::onTouchTap", loop_start)
        loop_block = SOURCE[loop_start:touch_start]
        self.assertIn("if (!pendingHomeAppArtifact.empty())", loop_block)
        self.assertIn(
            "resolveInstalledAppPath(artifact.c_str(), resolvedPath)",
            loop_block,
        )
        self.assertIn(
            "runNativeApp(resolvedPath.c_str(), renderer, mappedInput)",
            loop_block,
        )
        self.assertLess(
            loop_block.index("if (!pendingHomeAppArtifact.empty())"),
            loop_block.index("if (appsPending)"),
        )

    def test_apps_link_resolves_managed_or_bootstrap_springboard_each_launch(self):
        self.assertNotIn(
            '!std::strcmp(artifact, "springboard.elf")',
            RESOLVER,
        )
        managed_return = RESOLVER.index("if (!managedPath.empty())")
        legacy_recovery = RESOLVER.index("RuntimePackages::recoverAppPair(artifact)")
        self.assertLess(managed_return, legacy_recovery)

        start = HOST.index(
            "bool runNativeSpringboard(GfxRenderer& renderer, MappedInputManager& input, bool resume)"
        )
        block = HOST[start:]
        self.assertNotIn(
            'const char* springboard = "/sd/Apps/springboard.elf";',
            block,
        )
        resolve = 'resolveInstalledAppPath("springboard.elf", springboard)'
        launch = "runNativeApp(springboard.c_str(), renderer, input)"
        resolve_pos = block.index(resolve)
        launch_pos = block.index(launch)
        launch_loop = block.rfind("for (;;) {", 0, resolve_pos)
        self.assertGreater(launch_loop, 0)
        self.assertIn("std::string springboard;", block[launch_loop:resolve_pos])
        self.assertLess(launch_loop, resolve_pos)
        self.assertLess(resolve_pos, launch_pos)

    def test_pending_shortcut_is_reset_when_home_is_entered(self):
        enter_start = SOURCE.index("void HomeActivity::onEnter()")
        enter_end = SOURCE.index("\nvoid HomeActivity::onExit()", enter_start)
        self.assertIn(
            "pendingHomeAppArtifact.clear();",
            SOURCE[enter_start:enter_end],
        )

    def test_home_has_no_builtin_file_rows(self):
        self.assertNotIn("menuItems.push_back(tr(STR_FILE_TRANSFER))", SOURCE)
        self.assertNotIn("onFileTransferOpen()", SOURCE)
        self.assertNotIn("menuItems.push_back(tr(STR_BROWSE_FILES))", SOURCE)
        self.assertNotIn("onFileBrowserOpen()", SOURCE)
        self.assertIn("int count = 3 + static_cast<int>(homeApps.size())", SOURCE)

    def test_apps_row_uses_springboard_icon(self):
        apps_start = SOURCE.index("menuItems.push_back(tr(STR_APPS))")
        apps_end = SOURCE.index("for (const auto& app : homeApps)", apps_start)
        apps_block = SOURCE[apps_start:apps_end]
        self.assertIn('menuAppIcons.push_back("solid:f00a");', apps_block)

    def test_pinned_shortcuts_use_authored_font_awesome_face_at_menu_scale(self):
        for theme in (BASE_THEME, LYRA_THEME, ROUNDED_THEME):
            self.assertIn("constexpr int kAppIconSize = 12;", theme)
            self.assertIn("FontAwesomeIcons::draw(", theme)
            self.assertNotIn("FontAwesomeIcons::drawRegular", theme)

    def test_pinned_shortcuts_use_manifest_font_awesome_icons(self):
        self.assertIn("std::vector<const char*> menuAppIcons;", SOURCE)
        self.assertIn("menuAppIcons.push_back(app.icon);", SOURCE)
        self.assertIn(
            "[&menuAppIcons](int index) { return menuAppIcons[index]; }",
            SOURCE,
        )
        for theme in (BASE_THEME, LYRA_THEME, ROUNDED_THEME):
            self.assertIn("FontAwesomeIcons::draw(", theme)
            self.assertNotIn("FontAwesomeIcons::drawRegular", theme)
            self.assertIn("rowAppIcon", theme)


if __name__ == "__main__":
    unittest.main()
