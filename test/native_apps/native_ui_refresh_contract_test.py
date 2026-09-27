from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
bridge = (ROOT / "src/native/NativeUiBridge.cpp").read_text()
host = (ROOT / "src/native/NativeAppHost.cpp").read_text()

# All native UI frame presenters must use the serviced path. Directly pushing
# the EPD frame from loopTask starves IDLE0 on the T5S3 display driver.
assert bridge.count("presentNativeAppUiFrame()") == 3
assert "displayBuffer(" not in bridge
assert "HalDisplay::BALANCED_REFRESH" in host
assert "xPortGetCoreID() == 0 ? 1 : 0" in host
assert "vTaskDelay(1);" in host

# Package state flags stay ABI-compatible in t5_ui_list_row_t. The bridge
# passes semantic Font Awesome icons into the active theme rather than shrinking
# the list rectangle and painting icons afterward.
base_theme = (ROOT / "src/components/themes/BaseTheme.cpp").read_text()
lyra_theme = (ROOT / "src/components/themes/lyra/LyraThemeDrawB.cpp").read_text()
rounded_theme = (ROOT / "src/components/themes/roundedraff/RoundedRaffTheme.cpp").read_text()
assert 'T5_UI_LIST_ICON_DOWNLOAD' in bridge and '"solid:f019"' in bridge
assert 'T5_UI_LIST_ICON_UPDATE' in bridge and '"solid:f021"' in bridge
assert 'T5_UI_LIST_ICON_INSTALLED' in bridge and '"regular:f058"' in bridge
assert "stateIconGutter" not in bridge
# Adaptive display work must keep the theme-owned full-row icon rendering while
# deriving the list rectangle from the active display's safe viewport.
assert "struct NativeUiLayout" in bridge
assert "renderer.getOrientedViewableTRBL" in bridge
assert "const Rect content{layout.safeLeft, layout.contentTop, layout.safeWidth()," in bridge
assert "stateIconFn" in bridge and "TextRole::System, stateIconFn" in bridge
assert "FontAwesomeIcons::draw" not in bridge

# Every theme owns the leading icon inside its existing full-width row
# selection, so selected-row geometry cannot exclude the icon column.
assert "rowFontAwesomeIcon" in base_theme
assert "renderer.fillRect(rect.x" in base_theme
assert "FontAwesomeIcons::draw" in base_theme
assert "rowFontAwesomeIcon" in lyra_theme and "FontAwesomeIcons::draw" in lyra_theme
assert "Lyra selection is light gray, so state icons remain black" in lyra_theme
assert "rowFontAwesomeIcon" in rounded_theme and "FontAwesomeIcons::draw" in rounded_theme

print("Native UI full-row selection and themed Font Awesome list-state rendering PASS")
