/* Reuse the real host, class ELF loader and physical-controller fixture. The
 * production provider implementations, not a mock, own their inventories. */
#define main unused_host_class_integration_main
#include "usb_host_class_integration_test.c"
#undef main

static bool fail_configuration(void *ctx, uint64_t physical, uint8_t *bytes,
                               size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx; (void)physical; (void)bytes;
    (void)length; (void)vid; (void)pid;
    return false;
}

int main(int argc, char **argv) {
    assert(argc == 6);
    void *libraries[5] = {0};
    for (size_t i = 0; i < 5; ++i) {
        libraries[i] = dlopen(argv[i + 1], RTLD_NOW);
        assert(libraries[i]);
    }
    const risc_driver_v2 *host_driver = load(libraries[0], "usb-host-v2");
    const risc_driver_v2 *classes[] = {
        load(libraries[1], "usb-cdc-acm-v2"),
        load(libraries[2], "usb-cp210x-v2"),
        load(libraries[3], "usb-ch34x-v2"),
        load(libraries[4], "usb-serial-witness")
    };
    const uint16_t vids[] = {0x2341u, 0x10c4u, 0x1a86u, 0xcafeu};
    const uint16_t pids[] = {0x0043u, 0xea60u, 0x7523u, 0x4001u};
    risc_usb_controller_api_v1 controller = {
        RISC_USB_CONTROLLER_API_V1, sizeof(controller), NULL, next_event,
        configuration, claim, release_claim, control, bulk_read, bulk_write,
        physical_quiesce
    };
    risc_provider_dependency_v1 physical_dependency = {
        "usb.controller", 1, &controller
    };
    assert(host_driver->start(&physical_dependency, 1));
    const risc_usb_host_snapshot_v1 *host_api =
        (const risc_usb_host_snapshot_v1 *)host_driver->capability;
    assert(host_api && host_api->snapshot);
    risc_provider_dependency_v1 class_dependency = {
        "usb.host", 1, &host_api->discovery.host
    };
    uint64_t previous_generation = 0;
    for (size_t i = 0; i < 4; ++i) {
        active_vid = vids[i];
        active_pid = pids[i];
        const risc_driver_v2 *driver = classes[i];
        const risc_serial_port_api_v1 *port =
            (const risc_serial_port_api_v1 *)driver->capability;
        assert(port && port->api_version == RISC_SERIAL_PORT_API_V1 &&
               port->struct_size >= sizeof(risc_serial_port_inventory_v1));
        const risc_serial_port_inventory_v1 *inventory =
            (const risc_serial_port_inventory_v1 *)port;
        assert(inventory->discovery.probe && inventory->snapshot);
        assert(driver->start(&class_dependency, 1));
        risc_serial_device_v1 observed[RISC_SERIAL_INVENTORY_MAX_DEVICES] = {{0}};
        size_t count = RISC_SERIAL_INVENTORY_MAX_DEVICES;
        assert(inventory->snapshot(observed, &count) && count == 0);
        const uint64_t physical = ++physical_sequence;
        enqueue(1, physical);
        observed[0].provider_device = 0xdeadbeefu;
        count = 0;
        assert(!inventory->snapshot(observed, &count) && count == 1 &&
               observed[0].provider_device == 0xdeadbeefu);
        count = RISC_SERIAL_INVENTORY_MAX_DEVICES;
        assert(inventory->snapshot(observed, &count) && count == 1);
        const uint64_t token = snapshot_one(host_api);
        assert(token && observed[0].provider_device == token &&
               observed[0].generation == token && token != previous_generation &&
               observed[0].transport == RISC_SERIAL_TRANSPORT_USB);
        previous_generation = token;
        assert(claim_calls == 0 && control_calls == 0 && release_calls == 0);

        /* An unidentified-but-still-present host token is UNKNOWN, not an
         * empty healthy snapshot that revokes a live published device. */
        controller.configuration = fail_configuration;
        count = RISC_SERIAL_INVENTORY_MAX_DEVICES;
        observed[0].generation = 0xdeadbeefu;
        assert(!inventory->snapshot(observed, &count) &&
               observed[0].generation == 0xdeadbeefu);
        controller.configuration = configuration;
        count = RISC_SERIAL_INVENTORY_MAX_DEVICES;
        assert(inventory->snapshot(observed, &count) && count == 1 &&
               observed[0].provider_device == token);

        enqueue(2, physical);
        count = RISC_SERIAL_INVENTORY_MAX_DEVICES;
        assert(inventory->snapshot(observed, &count) && count == 0);
        assert(driver->quiesce());
        driver->stop();
        assert(!inventory->snapshot(observed, &count));
    }
    assert(host_driver->quiesce() && live_claims == 0 &&
           claim_calls == 0 && control_calls == 0 && release_calls == 0);
    host_driver->stop();
    for (size_t i = 5; i > 0; --i) assert(dlclose(libraries[i - 1]) == 0);
    puts("Production four-class provider-owned inventory: PASS");
    return 0;
}
