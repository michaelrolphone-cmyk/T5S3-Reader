"""Source wiring regression for the firmware and physical USB ELF lifecycle.

The separate provider graph host tests exercise failed quiescence and the
consumed-grant contract; the full physical ELF compile remains an independent
CI job, not a hardware acceptance claim.
"""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "src/native/NativeUsbBridge.cpp"
GRAPH = ROOT / "src/runtime/drivers/ProviderGraphV2.cpp"
CONTROLLER = ROOT / "Drivers/usb_controller_esp32s3/driver.cpp"
MANIFEST = ROOT / "Drivers/usb_controller_esp32s3/manifest.json"


class UsbTeardownRetry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bridge = BRIDGE.read_text(encoding="utf-8")
        cls.graph = GRAPH.read_text(encoding="utf-8")
        cls.controller = CONTROLLER.read_text(encoding="utf-8")

    def test_grant_consumption_is_followed_by_verified_graph_shutdown(self):
        release = self.graph.split("bool GraphV2::release(GrantV2 grant)", 1)[1].split(
            "size_t GraphV2::liveGrants()", 1
        )[0]
        self.assertLess(release.index("slot.occupied = false;"),
                        release.index("deactivateIfUnused(node)"))
        helper = self.bridge.split("bool releaseGrant(", 1)[1].split(
            "bool restoreAfterSafeShutdown()", 1
        )[0]
        self.assertIn("RuntimeInstalledProviders::release(&grant)", helper)
        self.assertIn("grant = {};", helper)
        recovery = self.bridge.split("bool restoreAfterSafeShutdown()", 1)[1].split(
            "bool codingValid(", 1
        )[0]
        self.assertLess(recovery.index("if (!RuntimeInstalledProviders::shutdown())"),
                        recovery.index("quarantined = false;"))
        self.assertLess(recovery.index("quarantined = false;"),
                        recovery.index("restoreDebugConsole();"))

    def test_session_close_and_grant_release_are_independent(self):
        close = self.bridge.split("bool closeClass()", 1)[1].split("bool openClass(", 1)[0]
        self.assertIn("if (session) {", close)
        self.assertIn("if (!serial || !serial->close(session))", close)
        self.assertLess(close.index("session = 0;"),
                        close.index('releaseGrant(classGrant, "class-grant-release")'))
        self.assertNotIn("if (!session) return true;", close)

    def test_app_exit_and_reopen_retry_only_safe_shutdown(self):
        stop = self.bridge.split("bool stopLocked()", 1)[1].split("bool serialStart(", 1)[0]
        startup = self.bridge.split("bool serialStart(", 1)[1].split("void serialStop()", 1)[0]
        exit_fn = self.bridge.split("void serialStop()", 1)[1].split("bool serialReadState(", 1)[0]
        self.assertNotIn("if (quarantined) return;", stop)
        self.assertIn("if (session) return false;", stop)
        self.assertIn('releaseGrant(hostGrant, "host-grant-release")', stop)
        self.assertIn("if (!restoreAfterSafeShutdown())", stop)
        self.assertIn("if (!stopLocked())", startup)
        self.assertIn("USBREF stage=serial-start-quarantined error=", startup)
        self.assertIn("(void)stopLocked();", exit_fn)

    def test_physical_claim_is_not_lost_when_device_close_is_deferred(self):
        release = self.controller.split("bool release_interface(", 1)[1].split(
            "int32_t control(", 1
        )[0]
        self.assertNotIn("Claim *c = claim(id);", release)
        self.assertIn("usb_host_interface_release(", release)
        self.assertLess(release.index("*c = {};"),
                        release.index("usb_host_device_close("))
        deferred = release.split("if (closed != ESP_OK)", 1)[1]
        self.assertIn("USBCTRL stage=device-close-deferred", deferred)
        self.assertIn("return true;", deferred)
        self.assertIn("*d = {};", release)
        quiesce = self.controller.split("bool quiesce(void *)", 1)[1].split(
            "void stop()", 1
        )[0]
        self.assertNotIn("if (fault || claimed(0)) return false;", quiesce)
        self.assertIn("if (inFlight && !drain_bulk(false)) return false;", quiesce)
        self.assertIn("USBCTRL cleanup-failed stage=outstanding-claim", quiesce)
        self.assertLess(quiesce.index("usb_host_uninstall()"),
                        quiesce.index("power->release_host("))
        self.assertIn("USBCTRL cleanup-failed stage=device-close", quiesce)
        self.assertEqual(json.loads(MANIFEST.read_text(encoding="utf-8"))["version"], "0.1.3")


if __name__ == "__main__":
    unittest.main()
