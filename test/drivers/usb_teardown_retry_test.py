"""Source wiring regression for the real firmware USB bridge lifecycle.

The separate provider graph host tests exercise failed quiescence and the
consumed-grant contract. This checks that the bridge actually uses those
semantics on both app exit and a subsequent open, without faking hardware.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "src/native/NativeUsbBridge.cpp"
GRAPH = ROOT / "src/runtime/drivers/ProviderGraphV2.cpp"


class UsbTeardownRetry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bridge = BRIDGE.read_text(encoding="utf-8")
        cls.graph = GRAPH.read_text(encoding="utf-8")

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


if __name__ == "__main__":
    unittest.main()
