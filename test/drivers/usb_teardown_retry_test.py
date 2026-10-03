"""Source wiring regression for firmware and physical USB ELF lifecycle.

The HID wrapper extends driver_base.cpp in the same physical ELF. Check the
actual verified teardown implementation, not the thin translation-unit entry.
"""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "src/native/NativeUsbBridge.cpp"
GRAPH = ROOT / "src/runtime/drivers/ProviderGraphV2.cpp"
CONTROLLER = ROOT / "Drivers/usb_controller_esp32s3/driver_base.cpp"
MANIFEST = ROOT / "Drivers/usb_controller_esp32s3/manifest.json"


class UsbTeardownRetry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bridge = BRIDGE.read_text(encoding="utf-8")
        cls.graph = GRAPH.read_text(encoding="utf-8")
        cls.controller = CONTROLLER.read_text(encoding="utf-8")

    def test_failed_release_retains_exact_grant_until_quiescence(self):
        # U1 retains the grant during quarantine, rather than consuming it and
        # then relying on a global shutdown which could disrupt another app.
        release = self.graph.split("bool GraphV2::release(GrantV2 grant)", 1)[1].split(
            "size_t GraphV2::liveGrants()", 1)[0]
        self.assertIn("slot.pendingRelease = true;", release)
        self.assertLess(release.index("deactivateIfUnused(node)"),
                        release.index("slot.occupied = false;"))
        session = (ROOT / "src/runtime/drivers/InstalledProviderSession.h").read_text()
        self.assertIn("if (!release(&lease_))", session)
        self.assertIn("recoverFailedProvider(failedProvider_", session)
        inventory = (ROOT / "src/runtime/drivers/InstalledSerialInventory.h").read_text()
        shutdown = inventory.split("bool stopChecked()", 1)[1]
        self.assertLess(shutdown.index("withdrawAll()"), shutdown.index("release(&slot.lease)"))
        self.assertLess(shutdown.index("release(&slot.lease)"), shutdown.index("slot = Slot{};"))

    def test_physical_claim_is_not_lost_when_device_close_is_deferred(self):
        release = self.controller.split("bool release_interface(", 1)[1].split(
            "int32_t control(", 1
        )[0]
        self.assertNotIn("Claim *c = claim(id);", release)
        self.assertIn("usb_host_interface_release(", release)
        self.assertIn("c->interfaceReleased", release)
        self.assertIn("RiscUsbController::releaseClaim(", release)
        self.assertLess(release.index("RiscUsbController::releaseClaim("),
                        release.index("*c = {};"))
        self.assertIn("if (detached && !others) *d = {};", release)
        quiesce = self.controller.split("bool quiesce_host()", 1)[1].split(
            "void stop()", 1
        )[0]
        self.assertNotIn("if (fault || claimed(0)) return false;", quiesce)
        self.assertIn("if (inFlight && !drain_bulk(false)) return false;", quiesce)
        self.assertIn("USBCTRL cleanup-failed stage=outstanding-claim", quiesce)
        self.assertLess(quiesce.index("usb_host_uninstall()"),
                        quiesce.index("power->release_host("))
        self.assertIn("USBCTRL cleanup-failed stage=device-close", quiesce)
        self.assertGreaterEqual(
            tuple(map(int, json.loads(MANIFEST.read_text(encoding="utf-8"))["version"].split("."))),
            (0, 1, 6))

    def test_partial_idle_cleanup_is_retried_before_client_pump(self):
        next_event = self.controller.split("int32_t next_event(", 1)[1].split(
            "bool configuration(", 1
        )[0]
        cleanup = next_event.index("UsbRoleSwitch::State::Cleanup")
        service = next_event.index("service_role();", cleanup)
        pump = next_event.index("pump(0)")
        self.assertLess(cleanup, service)
        self.assertLess(service, pump)
        self.assertIn("return 0;", next_event[service:pump])
        self.assertIn("partially complete", next_event)

    def test_idle_host_processes_no_clients_before_device_free(self):
        quiesce = self.controller.split("bool quiesce_host()", 1)[1].split(
            "void stop()", 1
        )[0]
        deregister = quiesce.index("usb_host_client_deregister(client)")
        no_clients_event = quiesce.index("USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS")
        free_all = quiesce.index("usb_host_device_free_all()")
        uninstall = quiesce.index("usb_host_uninstall()")
        self.assertLess(deregister, no_clients_event)
        self.assertLess(no_clients_event, free_all)
        self.assertLess(free_all, uninstall)
        self.assertIn("static bool noClientsObserved;", self.controller)
        self.assertIn("if (!noClientsObserved)", quiesce)
        self.assertIn("USBCTRL cleanup-failed stage=no-clients-timeout", quiesce)
        self.assertIn("USB_HOST_LIB_EVENT_FLAGS_ALL_FREE", quiesce)
        self.assertIn("bool freed = rc == ESP_OK;", quiesce)
        self.assertLess(uninstall, quiesce.index("power->release_host("))


if __name__ == "__main__":
    unittest.main()
