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

print("Native UI e-paper refresh: serviced on the opposite core with owner-task yield PASS")
