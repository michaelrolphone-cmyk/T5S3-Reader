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

# Package state flags stay ABI-compatible in t5_ui_list_row_t and are rendered
# by the shared native UI bridge with the shipped Font Awesome faces.
assert 'components/FontAwesomeIcons.h' in bridge
assert 'T5_UI_LIST_ICON_DOWNLOAD' in bridge and '"solid:f019"' in bridge
assert 'T5_UI_LIST_ICON_UPDATE' in bridge and '"solid:f021"' in bridge
assert 'T5_UI_LIST_ICON_INSTALLED' in bridge and '"solid:f00c"' in bridge
assert "FontAwesomeIcons::draw" in bridge
assert "stateIconGutter" in bridge
assert "const int iconX = metrics.contentSidePadding;" in bridge
assert "(rowHeight - stateIconSize) / 2" in bridge
assert "valueWidth" not in bridge

print("Native UI e-paper refresh and left-gutter Font Awesome list-state rendering PASS")
