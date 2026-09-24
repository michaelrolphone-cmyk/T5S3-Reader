#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../Apps/usb_debug.c"

static char saved_path[192];
static size_t saved_size;
static uint64_t last_claim_device;
static uint8_t last_claim_interface;
static unsigned release_count;

static const uint8_t device_descriptor[] = {
    18, 1, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
    0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 1, 2, 3, 1
};
static const uint8_t language_descriptor[] = {4, 3, 0x09, 0x04};
static const uint8_t manufacturer_descriptor[] = {
    16, 3, 'A',0,'c',0,'m',0,'e',0,' ',0,'U',0,'S',0
};
static const uint8_t product_descriptor[] = {
    20, 3, 'D',0,'e',0,'b',0,'u',0,'g',0,' ',0,'P',0,'a',0,'d',0
};
static const uint8_t serial_descriptor[] = {
    14, 3, 'A',0,'B',0,'C',0,'1',0,'2',0,'3',0
};
/* Config + interface + HID + interrupt endpoint. */
static const uint8_t configuration_descriptor[] = {
    9, 2, 34, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 1, 3, 1, 1, 0,
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, 4, 0,
    7, 5, 0x81, 3, 8, 0, 10
};
static const uint8_t hid_report[] = {0x05, 0x01, 0x09, 0x06};

static int32_t mock_control(void *context, uint64_t device,
                            uint8_t request_type, uint8_t request,
                            uint16_t value, uint16_t index,
                            uint8_t *payload, uint16_t length,
                            uint32_t timeout_ms) {
    (void)context;
    assert(device == 0x1122334455667788ULL);
    assert(payload);
    assert(timeout_ms == USB_DEBUG_CONTROL_TIMEOUT_MS);
    if (request == USB_REQ_GET_STATUS && request_type == 0x80u &&
        value == 0 && index == 0 && length == 2) {
        payload[0] = 3; payload[1] = 0;
        return 2;
    }
    if (request == USB_REQ_GET_CONFIGURATION && request_type == 0x80u &&
        value == 0 && index == 0 && length == 1) {
        payload[0] = 1;
        return 1;
    }
    if (request != USB_REQ_GET_DESCRIPTOR) return -1;

    const uint8_t type = (uint8_t)(value >> 8);
    const uint8_t descriptor_index = (uint8_t)value;
    const uint8_t *source = NULL;
    size_t source_length = 0;
    if (type == USB_DESC_DEVICE && descriptor_index == 0 && request_type == 0x80u) {
        source = device_descriptor; source_length = sizeof(device_descriptor);
    } else if (type == USB_DESC_CONFIGURATION && descriptor_index == 0 &&
               request_type == 0x80u) {
        source = configuration_descriptor; source_length = sizeof(configuration_descriptor);
    } else if (type == USB_DESC_STRING && descriptor_index == 0 &&
               request_type == 0x80u) {
        source = language_descriptor; source_length = sizeof(language_descriptor);
    } else if (type == USB_DESC_STRING && descriptor_index == 1 &&
               request_type == 0x80u) {
        source = manufacturer_descriptor; source_length = sizeof(manufacturer_descriptor);
    } else if (type == USB_DESC_STRING && descriptor_index == 2 &&
               request_type == 0x80u) {
        source = product_descriptor; source_length = sizeof(product_descriptor);
    } else if (type == USB_DESC_STRING && descriptor_index == 3 &&
               request_type == 0x80u) {
        source = serial_descriptor; source_length = sizeof(serial_descriptor);
    } else if (type == USB_DESC_REPORT && descriptor_index == 0 &&
               request_type == 0x81u && index == 0) {
        source = hid_report; source_length = sizeof(hid_report);
    } else {
        return -1;
    }
    if (length < source_length) source_length = length;
    memcpy(payload, source, source_length);
    return (int32_t)source_length;
}

static bool mock_configuration(void *context, uint64_t device, uint8_t *bytes,
                               size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)context;
    assert(device == 0x1122334455667788ULL);
    assert(bytes && length && vid && pid);
    if (*length < sizeof(configuration_descriptor)) {
        *length = sizeof(configuration_descriptor);
        return false;
    }
    memcpy(bytes, configuration_descriptor, sizeof(configuration_descriptor));
    *length = sizeof(configuration_descriptor);
    *vid = 0x1234;
    *pid = 0x5678;
    return true;
}

static bool mock_claim(void *context, uint64_t device, uint8_t interface_number,
                       uint8_t alternate, uint64_t *claim) {
    (void)context;
    assert(alternate == 0);
    last_claim_device = device;
    last_claim_interface = interface_number;
    *claim = 42;
    return true;
}

static void mock_release(void *context, uint64_t claim) {
    (void)context;
    assert(claim == 42);
    ++release_count;
}

static bool mock_write(const char *path, const void *data, size_t size) {
    assert(path && data && size);
    snprintf(saved_path, sizeof(saved_path), "%s", path);
    saved_size = size;
    return true;
}

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    (void)version;
    return NULL;
}
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    (void)version;
    return NULL;
}
const t5_storage_api_v1 *t5_storage_get_api(uint32_t version) {
    (void)version;
    return NULL;
}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t version) {
    (void)version;
    return NULL;
}

int main(void) {
    static const risc_usb_host_api_v1 host = {
        .api_version = RISC_USB_HOST_API_V1,
        .struct_size = sizeof(risc_usb_host_api_v1),
        .context = NULL,
        .configuration = mock_configuration,
        .claim = mock_claim,
        .release = mock_release,
        .control = mock_control,
    };
    static const t5_storage_api_v1 storage = {
        .api_version = T5_STORAGE_API_VERSION,
        .struct_size = sizeof(t5_storage_api_v1),
        .write_file_atomic = mock_write,
    };
    host_api = &host;
    storage_api = &storage;
    diagnostics_api = NULL;

    char clean[32] = {0};
    sanitize_identifier_part("A B/C:1", clean, sizeof(clean));
    assert(strcmp(clean, "A_B_C_1") == 0);

    usb_debug_device_t device = {0};
    assert(read_device_summary(0x1122334455667788ULL, &device));
    assert(device.vid == 0x1234 && device.pid == 0x5678);
    assert(strcmp(device.manufacturer, "Acme US") == 0);
    assert(strcmp(device.product, "Debug Pad") == 0);
    assert(strcmp(device.serial, "ABC123") == 0);
    assert(strcmp(device.identifier, "usb_1234_5678_ABC123") == 0);

    assert(build_report(&device));
    assert(strstr(log_buffer, "VID:PID: 1234:5678"));
    assert(strstr(log_buffer, "Self powered: yes"));
    assert(strstr(log_buffer, "Remote wakeup enabled: yes"));
    assert(strstr(log_buffer, "CURRENT CONFIGURATION: 1"));
    assert(strstr(log_buffer, "INTERFACE if=0 alt=0 eps=1 class=0x03 (HID)"));
    assert(strstr(log_buffer, "ENDPOINT 0x81 IN interrupt max_packet=8 interval=10"));
    assert(strstr(log_buffer, "HID report descriptor (4 bytes)"));
    assert(strstr(log_buffer, "0000: 05 01 09 06"));
    assert(strstr(log_buffer, "[3] ABC123"));
    assert(last_claim_device == device.token && last_claim_interface == 0);
    assert(release_count == 1);

    assert(save_report(&device));
    assert(strcmp(saved_path, "/sd/usb-debug/usb_1234_5678_ABC123.txt") == 0);
    assert(saved_size == log_length && saved_size > 0);

    puts("USB Debug descriptor capture, HID report read, identifier and SD log path PASS");
    return 0;
}
