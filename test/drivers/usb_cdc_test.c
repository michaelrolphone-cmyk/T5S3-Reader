#include <T5DriverApi.h>
#include <T5UsbClassDriver.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Config(9), communications interface(9), data interface(9), bulk IN(7),
// bulk OUT(7). Only a complete, bounded descriptor may be claimed.
static const uint8_t config[] = {
    9,2,41,0,2,1,0,0x80,50,
    9,4,2,0,0,2,2,1,0,
    9,4,3,0,2,10,0,0,0,
    7,5,0x81,2,64,0,0,
    7,5,0x02,2,64,0,0
};
int main(int argc, char** argv) {
    assert(argc == 2);
    void* elf = dlopen(argv[1], RTLD_NOW);
    assert(elf);
    t5_driver_get_fn get = (t5_driver_get_fn)dlsym(elf,"t5_driver_get");
    assert(get && !get(0) && !get(T5_DRIVER_ABI_VERSION + 1));
    const t5_driver_v1* drv = get(T5_DRIVER_ABI_VERSION);
    assert(drv && drv->struct_size >= sizeof(*drv));
    assert(strcmp(drv->driver_id, "usb-cdc-acm") == 0);
    assert(strcmp(drv->capability_id, T5_USB_CDC_CLASS_CAPABILITY) == 0);
    assert(drv->capability_api == T5_USB_CDC_CLASS_API_VERSION);
    assert(drv->start && drv->stop && drv->start(0));
    const t5_usb_cdc_class_api_v1* api = (const t5_usb_cdc_class_api_v1*)drv->capability;
    assert(api && api->api_version == T5_USB_CDC_CLASS_API_VERSION &&
           api->struct_size >= sizeof(*api) && api->probe && api->line_coding && api->control_lines);
    t5_usb_cdc_binding_v1 bound = {0};
    assert(api->probe(config, sizeof(config), 0x1234, 0x5678, &bound));
    assert(bound.control_interface == 2 && bound.data_interface == 3 &&
           bound.data_alternate == 0 && bound.ep_in == 0x81 &&
           bound.ep_out == 2 && bound.ep_in_mps == 64 && bound.ep_out_mps == 64);
    // WCH's CH343/CH9102 CDC profile must follow the same descriptor path.
    assert(api->probe(config, sizeof(config), 0x1a86, 0x55d3, &bound));
    uint8_t malformed[sizeof(config)];
    memcpy(malformed, config, sizeof(config));
    bound.control_interface = 99;
    assert(!api->probe(malformed, 8, 0, 0, &bound) && bound.control_interface == 99);
    malformed[0] = 0;
    assert(!api->probe(malformed, sizeof(malformed), 0, 0, &bound));
    memcpy(malformed, config, sizeof(config));
    malformed[2] = 42;
    assert(!api->probe(malformed, sizeof(malformed), 0, 0, &bound));
    memcpy(malformed, config, sizeof(config));
    malformed[27] = 0; // Zero-length endpoint descriptor must fail closed.
    assert(!api->probe(malformed, sizeof(malformed), 0, 0, &bound));
    memcpy(malformed, config, sizeof(config));
    malformed[34] = 0x83; // Distinct IN endpoint does not replace required OUT.
    assert(!api->probe(malformed, sizeof(malformed), 0, 0, &bound));
    uint8_t line[7] = {0};
    assert(api->line_coding(115200, 8, 0, 1, line));
    assert(line[0] == 0 && line[1] == 0xc2 && line[2] == 1 && line[3] == 0 &&
           line[4] == 0 && line[5] == 0 && line[6] == 8);
    assert(api->line_coding(9600, 7, 2, 2, line));
    assert(line[4] == 2 && line[5] == 2 && line[6] == 7);
    assert(!api->line_coding(0, 8, 0, 1, line));
    assert(!api->line_coding(9600, 8, 0, 3, line));
    assert(!api->line_coding(9600, 8, 0, 1, 0));
    assert(api->control_lines(0,0) == 0 && api->control_lines(1,0) == 1 &&
           api->control_lines(0,1) == 2 && api->control_lines(1,1) == 3);
    drv->stop();
    assert(dlclose(elf) == 0);
    puts("USB CDC ELF class-driver tests passed");
    return 0;
}
