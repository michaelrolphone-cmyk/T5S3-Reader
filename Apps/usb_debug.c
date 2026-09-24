#include "T5AppApi.h"
#include "T5ProviderCapabilityApi.h"
#include "T5StorageApi.h"
#include "T5UiApi.h"
#include "RiscUsbDiscoveryDiagnosticsV1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define USB_DEBUG_DEVICE_LIMIT 8u
#define USB_DEBUG_LOG_CAPACITY (24u * 1024u)
#define USB_DEBUG_STRING_BYTES 255u
#define USB_DEBUG_STRING_TEXT 96u
#define USB_DEBUG_IDENTIFIER 96u
#define USB_DEBUG_STATUS 160u
#define USB_DEBUG_CONTROL_TIMEOUT_MS 100u
#define USB_DEBUG_HID_REPORT_LIMIT 1024u
#define USB_REQ_GET_STATUS 0u
#define USB_REQ_GET_DESCRIPTOR 6u
#define USB_REQ_GET_CONFIGURATION 8u
#define USB_DESC_DEVICE 1u
#define USB_DESC_CONFIGURATION 2u
#define USB_DESC_STRING 3u
#define USB_DESC_INTERFACE 4u
#define USB_DESC_ENDPOINT 5u
#define USB_DESC_DEVICE_QUALIFIER 6u
#define USB_DESC_INTERFACE_ASSOCIATION 11u
#define USB_DESC_BOS 15u
#define USB_DESC_HID 0x21u
#define USB_DESC_REPORT 0x22u
#define USB_DESC_CS_INTERFACE 0x24u
#define USB_DESC_CS_ENDPOINT 0x25u

typedef struct {
    uint64_t token;
    uint16_t vid;
    uint16_t pid;
    uint16_t bcd_usb;
    uint16_t bcd_device;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t manufacturer_index;
    uint8_t product_index;
    uint8_t serial_index;
    uint8_t configuration_count;
    bool descriptor_ok;
    char manufacturer[USB_DEBUG_STRING_TEXT];
    char product[USB_DEBUG_STRING_TEXT];
    char serial[USB_DEBUG_STRING_TEXT];
    char identifier[USB_DEBUG_IDENTIFIER];
    char title[USB_DEBUG_STRING_TEXT];
    char subtitle[USB_DEBUG_STRING_TEXT];
    char value[32];
} usb_debug_device_t;

static const t5_app_api_v1 *app_api;
static const t5_ui_api_v1 *ui_api;
static const t5_storage_api_v1 *storage_api;
static const t5_provider_capability_api_v1 *capability_api;
static const risc_usb_host_api_v1 *host_api;
static const risc_usb_host_discovery_v1 *discovery_api;
static const risc_usb_host_diagnostics_v1 *diagnostics_api;
static t5_provider_capability_lease_t host_lease;

static uint8_t descriptor_buffer[RISC_USB_CONFIG_LIMIT];
/* HID report reads must not overwrite a configuration descriptor while it is being parsed. */
static uint8_t hid_report_buffer[USB_DEBUG_HID_REPORT_LIMIT];
static uint8_t string_buffer[USB_DEBUG_STRING_BYTES];
static char log_buffer[USB_DEBUG_LOG_CAPACITY];
static size_t log_length;
static bool log_truncated;
static char status_text[USB_DEBUG_STATUS];

static usb_debug_device_t devices[USB_DEBUG_DEVICE_LIMIT];
static t5_ui_list_row_t rows[USB_DEBUG_DEVICE_LIMIT];
static uint32_t device_count;

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static const char *class_name(uint8_t cls) {
    switch (cls) {
        case 0x00: return "Defined per interface";
        case 0x01: return "Audio";
        case 0x02: return "Communications";
        case 0x03: return "HID";
        case 0x05: return "Physical";
        case 0x06: return "Image";
        case 0x07: return "Printer";
        case 0x08: return "Mass storage";
        case 0x09: return "Hub";
        case 0x0a: return "CDC data";
        case 0x0b: return "Smart card";
        case 0x0d: return "Content security";
        case 0x0e: return "Video";
        case 0x0f: return "Personal healthcare";
        case 0x10: return "Audio/video";
        case 0x11: return "Billboard";
        case 0x12: return "USB Type-C bridge";
        case 0xdc: return "Diagnostic";
        case 0xe0: return "Wireless controller";
        case 0xef: return "Miscellaneous";
        case 0xfe: return "Application specific";
        case 0xff: return "Vendor specific";
        default: return "Unknown";
    }
}

static const char *transfer_name(uint8_t attributes) {
    switch (attributes & 0x03u) {
        case 0: return "control";
        case 1: return "isochronous";
        case 2: return "bulk";
        case 3: return "interrupt";
        default: return "unknown";
    }
}

static const char *descriptor_name(uint8_t type) {
    switch (type) {
        case USB_DESC_DEVICE: return "DEVICE";
        case USB_DESC_CONFIGURATION: return "CONFIGURATION";
        case USB_DESC_STRING: return "STRING";
        case USB_DESC_INTERFACE: return "INTERFACE";
        case USB_DESC_ENDPOINT: return "ENDPOINT";
        case USB_DESC_DEVICE_QUALIFIER: return "DEVICE_QUALIFIER";
        case USB_DESC_INTERFACE_ASSOCIATION: return "IAD";
        case USB_DESC_BOS: return "BOS";
        case USB_DESC_HID: return "HID";
        case USB_DESC_CS_INTERFACE: return "CLASS_INTERFACE";
        case USB_DESC_CS_ENDPOINT: return "CLASS_ENDPOINT";
        default: return "UNKNOWN";
    }
}

static void log_reset(void) {
    log_length = 0;
    log_truncated = false;
    log_buffer[0] = '\0';
}

static void log_append(const char *format, ...) {
    if (log_truncated || log_length >= sizeof(log_buffer) - 1u) return;
    va_list args;
    va_start(args, format);
    const size_t remaining = sizeof(log_buffer) - log_length;
    int written = vsnprintf(log_buffer + log_length, remaining, format, args);
    va_end(args);
    if (written < 0) return;
    if ((size_t)written >= remaining) {
        log_length = sizeof(log_buffer) - 1u;
        log_buffer[log_length] = '\0';
        log_truncated = true;
        return;
    }
    log_length += (size_t)written;
}

static void log_hex(const uint8_t *bytes, size_t length, const char *prefix) {
    if (!bytes) return;
    for (size_t pos = 0; pos < length && !log_truncated; pos += 16u) {
        const size_t line = (length - pos) < 16u ? (length - pos) : 16u;
        log_append("%s%04lx: ", prefix ? prefix : "", (unsigned long)pos);
        for (size_t i = 0; i < line; ++i)
            log_append("%02x%s", (unsigned)bytes[pos + i], i + 1u == line ? "" : " ");
        log_append("\n");
    }
}

static bool host_get_descriptor(uint64_t token, uint8_t type, uint8_t index,
                                uint16_t language, uint8_t *out,
                                uint16_t capacity, uint16_t *size_out) {
    if (size_out) *size_out = 0;
    if (!host_api || !host_api->control || !out || !capacity) return false;
    int32_t count = host_api->control(host_api->context, token, 0x80u,
                                      USB_REQ_GET_DESCRIPTOR,
                                      (uint16_t)((uint16_t)type << 8) | index,
                                      language, out, capacity,
                                      USB_DEBUG_CONTROL_TIMEOUT_MS);
    if (count < 2 || count > capacity || out[1] != type || out[0] < 2u ||
        (uint16_t)out[0] > (uint16_t)count) return false;
    if (size_out) *size_out = (uint16_t)count;
    return true;
}

static bool host_control_in(uint64_t token, uint8_t request_type,
                            uint8_t request, uint16_t value, uint16_t index,
                            uint8_t *out, uint16_t capacity,
                            uint16_t *size_out) {
    if (size_out) *size_out = 0;
    if (!host_api || !host_api->control || !out || !capacity) return false;
    const int32_t count = host_api->control(
        host_api->context, token, request_type, request, value, index,
        out, capacity, USB_DEBUG_CONTROL_TIMEOUT_MS);
    if (count < 0 || count > capacity) return false;
    if (size_out) *size_out = (uint16_t)count;
    return true;
}

static uint16_t preferred_language(uint64_t token) {
    uint16_t count = 0;
    if (!host_get_descriptor(token, USB_DESC_STRING, 0, 0, string_buffer,
                             sizeof(string_buffer), &count) ||
        count < 4u || string_buffer[0] < 4u) return 0x0409u;
    return le16(string_buffer + 2);
}

static void utf16le_to_utf8(const uint8_t *bytes, size_t length,
                            char *out, size_t capacity) {
    if (!out || !capacity) return;
    size_t used = 0;
    out[0] = '\0';
    if (!bytes || length < 2u) return;
    size_t limit = bytes[0] < length ? bytes[0] : length;
    if (limit < 2u) return;
    for (size_t pos = 2u; pos + 1u < limit && used + 1u < capacity; pos += 2u) {
        uint16_t code = le16(bytes + pos);
        if (code >= 0xd800u && code <= 0xdfffu) code = '?';
        if (code < 0x80u) {
            out[used++] = (char)code;
        } else if (code < 0x800u && used + 2u < capacity) {
            out[used++] = (char)(0xc0u | (code >> 6));
            out[used++] = (char)(0x80u | (code & 0x3fu));
        } else if (used + 3u < capacity) {
            out[used++] = (char)(0xe0u | (code >> 12));
            out[used++] = (char)(0x80u | ((code >> 6) & 0x3fu));
            out[used++] = (char)(0x80u | (code & 0x3fu));
        } else {
            break;
        }
    }
    out[used] = '\0';
}

static bool read_string(uint64_t token, uint8_t index, uint16_t language,
                        char *out, size_t capacity) {
    if (!out || !capacity) return false;
    out[0] = '\0';
    if (!index) return false;
    uint16_t count = 0;
    if (!host_get_descriptor(token, USB_DESC_STRING, index, language,
                             string_buffer, sizeof(string_buffer), &count)) {
        if (language != 0x0409u &&
            host_get_descriptor(token, USB_DESC_STRING, index, 0x0409u,
                                string_buffer, sizeof(string_buffer), &count)) {
            utf16le_to_utf8(string_buffer, count, out, capacity);
            return out[0] != '\0';
        }
        return false;
    }
    utf16le_to_utf8(string_buffer, count, out, capacity);
    return out[0] != '\0';
}

static void sanitize_identifier_part(const char *source, char *out, size_t capacity) {
    if (!out || !capacity) return;
    size_t used = 0;
    if (source) {
        for (; *source && used + 1u < capacity; ++source) {
            const unsigned char c = (unsigned char)*source;
            const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            const bool digit = c >= '0' && c <= '9';
            out[used++] = alpha || digit || c == '-' || c == '_' ? (char)c : '_';
        }
    }
    out[used] = '\0';
}

static void make_identifier(usb_debug_device_t *device) {
    char serial[40] = {0};
    sanitize_identifier_part(device->serial, serial, sizeof(serial));
    if (serial[0])
        snprintf(device->identifier, sizeof(device->identifier),
                 "usb_%04x_%04x_%s", (unsigned)device->vid,
                 (unsigned)device->pid, serial);
    else
        snprintf(device->identifier, sizeof(device->identifier),
                 "usb_%04x_%04x", (unsigned)device->vid, (unsigned)device->pid);
}

static bool read_device_summary(uint64_t token, usb_debug_device_t *device) {
    memset(device, 0, sizeof(*device));
    device->token = token;
    uint16_t count = 0;
    if (!host_get_descriptor(token, USB_DESC_DEVICE, 0, 0, descriptor_buffer,
                             18u, &count) || count < 18u ||
        descriptor_buffer[0] < 18u) {
        snprintf(device->title, sizeof(device->title), "USB device");
        snprintf(device->subtitle, sizeof(device->subtitle),
                 "Device descriptor unavailable");
        snprintf(device->value, sizeof(device->value), "token %08lx",
                 (unsigned long)(uint32_t)token);
        return false;
    }
    device->descriptor_ok = true;
    device->bcd_usb = le16(descriptor_buffer + 2);
    device->device_class = descriptor_buffer[4];
    device->device_subclass = descriptor_buffer[5];
    device->device_protocol = descriptor_buffer[6];
    device->vid = le16(descriptor_buffer + 8);
    device->pid = le16(descriptor_buffer + 10);
    device->bcd_device = le16(descriptor_buffer + 12);
    device->manufacturer_index = descriptor_buffer[14];
    device->product_index = descriptor_buffer[15];
    device->serial_index = descriptor_buffer[16];
    device->configuration_count = descriptor_buffer[17];

    const uint16_t language = preferred_language(token);
    (void)read_string(token, device->manufacturer_index, language,
                      device->manufacturer, sizeof(device->manufacturer));
    (void)read_string(token, device->product_index, language,
                      device->product, sizeof(device->product));
    (void)read_string(token, device->serial_index, language,
                      device->serial, sizeof(device->serial));
    make_identifier(device);

    snprintf(device->title, sizeof(device->title), "%s",
             device->product[0] ? device->product : "USB device");
    snprintf(device->subtitle, sizeof(device->subtitle), "%s",
             device->manufacturer[0] ? device->manufacturer :
             class_name(device->device_class));
    snprintf(device->value, sizeof(device->value), "%04X:%04X",
             (unsigned)device->vid, (unsigned)device->pid);
    return true;
}

static bool read_active_configuration(uint64_t token, uint16_t *vid,
                                      uint16_t *pid, size_t *length) {
    if (!host_api || !host_api->configuration || !length) return false;
    *length = sizeof(descriptor_buffer);
    return host_api->configuration(host_api->context, token, descriptor_buffer,
                                   length, vid, pid);
}

static bool read_configuration_descriptor(uint64_t token, uint8_t index,
                                          uint8_t *buffer, size_t capacity,
                                          size_t *length) {
    if (!buffer || capacity < 9u || !length) return false;
    uint16_t first = 0;
    if (!host_get_descriptor(token, USB_DESC_CONFIGURATION, index, 0,
                             buffer, 9u, &first) || first < 9u) return false;
    const size_t total = le16(buffer + 2);
    if (total < 9u || total > capacity || total > UINT16_MAX) return false;
    uint16_t count = 0;
    if (!host_get_descriptor(token, USB_DESC_CONFIGURATION, index, 0,
                             buffer, (uint16_t)total, &count) ||
        count != total) return false;
    *length = total;
    return true;
}

static void append_device_descriptor(const usb_debug_device_t *device,
                                     const uint8_t *raw, size_t raw_length) {
    log_append("DEVICE DESCRIPTOR\n");
    log_append("  USB version: %x.%02x\n", (unsigned)(device->bcd_usb >> 8),
               (unsigned)(device->bcd_usb & 0xffu));
    log_append("  Class: 0x%02x (%s)\n", (unsigned)device->device_class,
               class_name(device->device_class));
    log_append("  Subclass: 0x%02x\n", (unsigned)device->device_subclass);
    log_append("  Protocol: 0x%02x\n", (unsigned)device->device_protocol);
    log_append("  VID:PID: %04X:%04X\n", (unsigned)device->vid,
               (unsigned)device->pid);
    log_append("  Device release: %x.%02x\n",
               (unsigned)(device->bcd_device >> 8),
               (unsigned)(device->bcd_device & 0xffu));
    log_append("  Manufacturer index: %u\n", (unsigned)device->manufacturer_index);
    log_append("  Product index: %u\n", (unsigned)device->product_index);
    log_append("  Serial index: %u\n", (unsigned)device->serial_index);
    log_append("  Configuration count: %u\n", (unsigned)device->configuration_count);
    log_append("  Manufacturer: %s\n",
               device->manufacturer[0] ? device->manufacturer : "(unavailable)");
    log_append("  Product: %s\n",
               device->product[0] ? device->product : "(unavailable)");
    log_append("  Serial: %s\n",
               device->serial[0] ? device->serial : "(unavailable)");
    log_append("  Raw:\n");
    log_hex(raw, raw_length, "    ");
    log_append("\n");
}

static void remember_string_index(uint8_t index, uint8_t *indices, size_t *count) {
    if (!index || !indices || !count || *count >= 32u) return;
    for (size_t i = 0; i < *count; ++i) if (indices[i] == index) return;
    indices[(*count)++] = index;
}

static void append_hid_report(uint64_t token, uint8_t interface_number,
                              uint8_t alternate, uint16_t requested_length) {
    if (!requested_length) return;
    if (requested_length > USB_DEBUG_HID_REPORT_LIMIT)
        requested_length = USB_DEBUG_HID_REPORT_LIMIT;
    uint64_t claim = 0;
    if (!host_api->claim ||
        !host_api->claim(host_api->context, token, interface_number,
                         alternate, &claim) || !claim) {
        log_append("    HID report descriptor: interface busy/unavailable\n");
        return;
    }
    /* HID report descriptors are opaque report bytes, not USB descriptor
     * records: unlike DEVICE/CONFIG/STRING, byte 1 is not descriptor type
     * 0x22. Issue the standard interface-recipient request directly. */
    const int32_t actual = host_api->control(
        host_api->context, token, 0x81u, USB_REQ_GET_DESCRIPTOR,
        (uint16_t)((uint16_t)USB_DESC_REPORT << 8), interface_number,
        hid_report_buffer, requested_length, USB_DEBUG_CONTROL_TIMEOUT_MS);
    if (actual > 0 && actual <= requested_length) {
        log_append("    HID report descriptor (%ld bytes%s):\n",
                   (long)actual,
                   actual == USB_DEBUG_HID_REPORT_LIMIT ? ", capped" : "");
        log_hex(hid_report_buffer, (size_t)actual, "      ");
    } else {
        log_append("    HID report descriptor: read failed\n");
    }
    if (host_api->release) host_api->release(host_api->context, claim);
}

static bool append_configuration(uint64_t token, uint8_t index,
                                 uint8_t *string_indices,
                                 size_t *string_index_count) {
    size_t length = 0;
    if (!read_configuration_descriptor(token, index, descriptor_buffer,
                                       sizeof(descriptor_buffer), &length)) {
        log_append("CONFIGURATION %u: unavailable\n\n", (unsigned)index);
        return false;
    }
    log_append("CONFIGURATION %u (%lu bytes)\n", (unsigned)index,
               (unsigned long)length);
    size_t pos = 0;
    uint8_t current_interface = 0xffu;
    uint8_t current_alternate = 0;
    uint8_t current_class = 0;
    uint16_t hid_report_length = 0;
    bool hid_probe_pending = false;
    while (pos + 2u <= length) {
        const uint8_t size = descriptor_buffer[pos];
        const uint8_t type = descriptor_buffer[pos + 1u];
        if (size < 2u || pos + size > length) {
            log_append("  MALFORMED descriptor at offset %lu: len=%u type=0x%02x\n",
                       (unsigned long)pos, (unsigned)size, (unsigned)type);
            break;
        }
        if (type == USB_DESC_CONFIGURATION && size >= 9u) {
            log_append("  CONFIG value=%u interfaces=%u total=%u attrs=0x%02x max_power=%u mA iConfig=%u\n",
                       (unsigned)descriptor_buffer[pos + 5u],
                       (unsigned)descriptor_buffer[pos + 4u],
                       (unsigned)le16(descriptor_buffer + pos + 2u),
                       (unsigned)descriptor_buffer[pos + 7u],
                       (unsigned)descriptor_buffer[pos + 8u] * 2u,
                       (unsigned)descriptor_buffer[pos + 6u]);
            remember_string_index(descriptor_buffer[pos + 6u],
                                  string_indices, string_index_count);
        } else if (type == USB_DESC_INTERFACE_ASSOCIATION && size >= 8u) {
            log_append("  IAD first_if=%u count=%u class=0x%02x (%s) subclass=0x%02x protocol=0x%02x iFunction=%u\n",
                       (unsigned)descriptor_buffer[pos + 2u],
                       (unsigned)descriptor_buffer[pos + 3u],
                       (unsigned)descriptor_buffer[pos + 4u],
                       class_name(descriptor_buffer[pos + 4u]),
                       (unsigned)descriptor_buffer[pos + 5u],
                       (unsigned)descriptor_buffer[pos + 6u],
                       (unsigned)descriptor_buffer[pos + 7u]);
            remember_string_index(descriptor_buffer[pos + 7u],
                                  string_indices, string_index_count);
        } else if (type == USB_DESC_INTERFACE && size >= 9u) {
            if (hid_probe_pending) {
                append_hid_report(token, current_interface, current_alternate,
                                  hid_report_length);
                hid_probe_pending = false;
                hid_report_length = 0;
            }
            current_interface = descriptor_buffer[pos + 2u];
            current_alternate = descriptor_buffer[pos + 3u];
            current_class = descriptor_buffer[pos + 5u];
            log_append("  INTERFACE if=%u alt=%u eps=%u class=0x%02x (%s) subclass=0x%02x protocol=0x%02x iInterface=%u\n",
                       (unsigned)current_interface,
                       (unsigned)current_alternate,
                       (unsigned)descriptor_buffer[pos + 4u],
                       (unsigned)current_class, class_name(current_class),
                       (unsigned)descriptor_buffer[pos + 6u],
                       (unsigned)descriptor_buffer[pos + 7u],
                       (unsigned)descriptor_buffer[pos + 8u]);
            remember_string_index(descriptor_buffer[pos + 8u],
                                  string_indices, string_index_count);
        } else if (type == USB_DESC_ENDPOINT && size >= 7u) {
            const uint8_t address = descriptor_buffer[pos + 2u];
            log_append("    ENDPOINT 0x%02x %s %s max_packet=%u interval=%u\n",
                       (unsigned)address,
                       (address & 0x80u) ? "IN" : "OUT",
                       transfer_name(descriptor_buffer[pos + 3u]),
                       (unsigned)le16(descriptor_buffer + pos + 4u),
                       (unsigned)descriptor_buffer[pos + 6u]);
        } else if (type == USB_DESC_HID && size >= 9u) {
            log_append("    HID bcdHID=%x.%02x country=%u subordinate=%u\n",
                       (unsigned)(le16(descriptor_buffer + pos + 2u) >> 8),
                       (unsigned)(le16(descriptor_buffer + pos + 2u) & 0xffu),
                       (unsigned)descriptor_buffer[pos + 4u],
                       (unsigned)descriptor_buffer[pos + 5u]);
            const uint8_t subordinate = descriptor_buffer[pos + 5u];
            for (uint8_t n = 0; n < subordinate; ++n) {
                const size_t entry = pos + 6u + (size_t)n * 3u;
                if (entry + 3u > pos + size) break;
                const uint8_t subtype = descriptor_buffer[entry];
                const uint16_t sublength = le16(descriptor_buffer + entry + 1u);
                log_append("      subordinate type=0x%02x length=%u\n",
                           (unsigned)subtype, (unsigned)sublength);
                if (subtype == USB_DESC_REPORT && current_class == 0x03u) {
                    hid_report_length = sublength;
                    hid_probe_pending = true;
                }
            }
        } else {
            log_append("    %s type=0x%02x len=%u raw=",
                       descriptor_name(type), (unsigned)type, (unsigned)size);
            for (uint8_t i = 0; i < size; ++i)
                log_append("%02x%s", (unsigned)descriptor_buffer[pos + i],
                           i + 1u == size ? "" : " ");
            log_append("\n");
        }
        pos += size;
    }
    if (hid_probe_pending)
        append_hid_report(token, current_interface, current_alternate,
                          hid_report_length);
    log_append("  Raw configuration bytes:\n");
    log_hex(descriptor_buffer, length, "    ");
    log_append("\n");
    return true;
}

static void append_optional_descriptor(uint64_t token, uint8_t type,
                                       const char *label, uint16_t first_read,
                                       uint16_t total_offset) {
    uint16_t first = 0;
    if (!host_get_descriptor(token, type, 0, 0, descriptor_buffer,
                             first_read, &first)) return;
    uint16_t total = first;
    if (total_offset && first >= total_offset + 2u)
        total = le16(descriptor_buffer + total_offset);
    if (total > sizeof(descriptor_buffer)) total = sizeof(descriptor_buffer);
    uint16_t actual = first;
    if (total > first &&
        !host_get_descriptor(token, type, 0, 0, descriptor_buffer,
                             total, &actual)) return;
    log_append("%s (%u bytes)\n", label, (unsigned)actual);
    log_hex(descriptor_buffer, actual, "  ");
    log_append("\n");
}

static void append_string_table(uint64_t token, uint16_t language,
                                uint8_t *indices, size_t count) {
    log_append("STRING DESCRIPTORS\n");
    uint16_t language_bytes = 0;
    if (host_get_descriptor(token, USB_DESC_STRING, 0, 0, string_buffer,
                            sizeof(string_buffer), &language_bytes)) {
        log_append("  Languages raw:\n");
        log_hex(string_buffer, language_bytes, "    ");
    }
    log_append("  Preferred language: 0x%04X\n", (unsigned)language);
    for (size_t i = 0; i < count; ++i) {
        char value[USB_DEBUG_STRING_TEXT] = {0};
        uint16_t raw_bytes = 0;
        const bool raw_ok = host_get_descriptor(
            token, USB_DESC_STRING, indices[i], language,
            string_buffer, sizeof(string_buffer), &raw_bytes);
        if (raw_ok) {
            utf16le_to_utf8(string_buffer, raw_bytes, value, sizeof(value));
            log_append("  [%u] %s\n", (unsigned)indices[i],
                       value[0] ? value : "(empty)");
            log_hex(string_buffer, raw_bytes, "    ");
        } else if (read_string(token, indices[i], language, value, sizeof(value))) {
            log_append("  [%u] %s (fallback language)\n",
                       (unsigned)indices[i], value);
        } else {
            log_append("  [%u] (unavailable)\n", (unsigned)indices[i]);
        }
    }
    log_append("\n");
}

static bool build_report(const usb_debug_device_t *device) {
    log_reset();
    log_append("RiscRTE USB Debug Report\n");
    log_append("========================\n");
    log_append("Identifier: %s\n", device->identifier[0] ?
               device->identifier : "(unavailable)");
    log_append("Host token: 0x%08lx%08lx\n",
               (unsigned long)(uint32_t)(device->token >> 32),
               (unsigned long)(uint32_t)device->token);
    if (diagnostics_api && diagnostics_api->diagnostic) {
        char diagnostic[160] = {0};
        if (diagnostics_api->diagnostic(host_api->context, diagnostic,
                                        sizeof(diagnostic)) && diagnostic[0])
            log_append("Host diagnostic: %s\n", diagnostic);
    }
    log_append("\n");

    uint16_t device_length = 0;
    if (!host_get_descriptor(device->token, USB_DESC_DEVICE, 0, 0,
                             descriptor_buffer, 18u, &device_length) ||
        device_length < 18u) {
        log_append("Device descriptor unavailable.\n");
        return false;
    }
    append_device_descriptor(device, descriptor_buffer, device_length);

    uint8_t status_bytes[2] = {0};
    uint16_t status_count = 0;
    if (host_control_in(device->token, 0x80u, USB_REQ_GET_STATUS, 0, 0,
                        status_bytes, sizeof(status_bytes), &status_count) &&
        status_count == sizeof(status_bytes)) {
        const uint16_t flags = le16(status_bytes);
        log_append("DEVICE STATUS\n");
        log_append("  Raw: 0x%04X\n", (unsigned)flags);
        log_append("  Self powered: %s\n", (flags & 0x0001u) ? "yes" : "no");
        log_append("  Remote wakeup enabled: %s\n",
                   (flags & 0x0002u) ? "yes" : "no");
        log_append("\n");
    } else {
        log_append("DEVICE STATUS unavailable\n\n");
    }

    uint8_t configuration_value = 0;
    uint16_t configuration_count = 0;
    if (host_control_in(device->token, 0x80u, USB_REQ_GET_CONFIGURATION, 0, 0,
                        &configuration_value, 1u, &configuration_count) &&
        configuration_count == 1u)
        log_append("CURRENT CONFIGURATION: %u\n\n",
                   (unsigned)configuration_value);
    else
        log_append("CURRENT CONFIGURATION unavailable\n\n");

    log_append("HOST METADATA\n");
    log_append("  usb.host@1 device token is provider-local and generation-qualified.\n");
    log_append("  Bus address, negotiated speed and hub path are not exposed by the current usb.host@1 ABI.\n\n");

    uint8_t string_indices[32] = {0};
    size_t string_index_count = 0;
    remember_string_index(device->manufacturer_index, string_indices,
                          &string_index_count);
    remember_string_index(device->product_index, string_indices,
                          &string_index_count);
    remember_string_index(device->serial_index, string_indices,
                          &string_index_count);

    uint16_t active_vid = 0, active_pid = 0;
    size_t active_length = 0;
    if (read_active_configuration(device->token, &active_vid, &active_pid,
                                  &active_length)) {
        log_append("ACTIVE CONFIGURATION SNAPSHOT\n");
        log_append("  VID:PID from host: %04X:%04X\n",
                   (unsigned)active_vid, (unsigned)active_pid);
        log_append("  Length: %lu\n", (unsigned long)active_length);
        log_append("  Raw:\n");
        log_hex(descriptor_buffer, active_length, "    ");
        log_append("\n");
    } else {
        log_append("ACTIVE CONFIGURATION SNAPSHOT unavailable\n\n");
    }

    for (uint8_t index = 0;
         index < device->configuration_count && index < 16u && !log_truncated;
         ++index)
        (void)append_configuration(device->token, index, string_indices,
                                   &string_index_count);

    append_optional_descriptor(device->token, USB_DESC_DEVICE_QUALIFIER,
                               "DEVICE QUALIFIER", 10u, 0u);
    append_optional_descriptor(device->token, USB_DESC_BOS,
                               "BOS DESCRIPTOR", 5u, 2u);

    append_string_table(device->token, preferred_language(device->token),
                        string_indices, string_index_count);

    if (device->configuration_count > 16u)
        log_append("NOTE: configuration list capped at 16 of %u entries.\n",
                   (unsigned)device->configuration_count);
    if (log_truncated) {
        static const char truncated[] = "\n[REPORT TRUNCATED AT 24 KiB]\n";
        const size_t marker = sizeof(truncated) - 1u;
        if (sizeof(log_buffer) > marker + 1u) {
            const size_t start = sizeof(log_buffer) - marker - 1u;
            memcpy(log_buffer + start, truncated, marker + 1u);
            log_length = start + marker;
        }
    }
    return true;
}

static void update_rows(void) {
    for (uint32_t i = 0; i < device_count; ++i) {
        rows[i].title = devices[i].title;
        rows[i].subtitle = devices[i].subtitle;
        rows[i].value = devices[i].value;
        rows[i].flags = devices[i].descriptor_ok ? T5_UI_LIST_HIGHLIGHT_VALUE : 0;
    }
}

static void host_diagnostic(char *out, size_t capacity) {
    if (!out || !capacity) return;
    out[0] = '\0';
    if (diagnostics_api && diagnostics_api->diagnostic &&
        diagnostics_api->diagnostic(host_api->context, out, capacity) && out[0])
        return;
    snprintf(out, capacity, "USB host active; waiting for device");
}

static bool refresh_devices(void) {
    size_t processed = 0;
    if (!discovery_api->poll(host_api->context, 16u, &processed)) {
        snprintf(status_text, sizeof(status_text), "USB host poll failed");
        return false;
    }
    uint64_t tokens[USB_DEBUG_DEVICE_LIMIT] = {0};
    size_t count = USB_DEBUG_DEVICE_LIMIT;
    if (!discovery_api->devices(host_api->context, tokens, &count)) {
        if (count > USB_DEBUG_DEVICE_LIMIT)
            snprintf(status_text, sizeof(status_text),
                     "%lu USB devices; app limit is %u",
                     (unsigned long)count, USB_DEBUG_DEVICE_LIMIT);
        else
            snprintf(status_text, sizeof(status_text), "USB device snapshot failed");
        return false;
    }
    device_count = (uint32_t)count;
    for (uint32_t i = 0; i < device_count; ++i)
        (void)read_device_summary(tokens[i], &devices[i]);
    update_rows();
    if (device_count)
        snprintf(status_text, sizeof(status_text), "%u device%s attached",
                 (unsigned)device_count, device_count == 1u ? "" : "s");
    else
        host_diagnostic(status_text, sizeof(status_text));
    return true;
}

static void render_main(int32_t selected) {
    const t5_ui_chrome_t chrome = {
        .title = "USB Debug",
        .subtitle = "usb.host descriptor inspector",
        .status = status_text,
        .back_label = "Back",
        .confirm_label = device_count ? "Inspect" : "Retry",
        .previous_label = "Up",
        .next_label = "Down",
    };
    if (device_count) {
        ui_api->render_list(&chrome, rows, device_count, selected);
    } else {
        const t5_ui_list_row_t empty = {
            .title = "No USB device enumerated",
            .subtitle = "Attach a device; Confirm retries",
            .value = "",
            .flags = 0,
        };
        ui_api->render_list(&chrome, &empty, 1u, 0);
    }
}

static bool save_report(const usb_debug_device_t *device) {
    if (!storage_api || !storage_api->write_file_atomic || !device ||
        !device->identifier[0] || !log_length) return false;
    char path[160] = {0};
    snprintf(path, sizeof(path), "/sd/usb-debug/%s.txt", device->identifier);
    if (!storage_api->write_file_atomic(path, log_buffer, log_length)) {
        snprintf(status_text, sizeof(status_text), "Save failed: %s", path);
        return false;
    }
    snprintf(status_text, sizeof(status_text), "Saved %s", path);
    return true;
}

static void render_report(const usb_debug_device_t *device,
                          int32_t scroll_from_bottom,
                          t5_ui_text_view_result_t *result) {
    char subtitle[128] = {0};
    snprintf(subtitle, sizeof(subtitle), "%s  %04X:%04X",
             device->product[0] ? device->product : "USB device",
             (unsigned)device->vid, (unsigned)device->pid);
    const t5_ui_chrome_t chrome = {
        .title = "USB Device Report",
        .subtitle = subtitle,
        .status = status_text,
        .back_label = "Devices",
        .confirm_label = "Save Log",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui_api->render_text_view(&chrome, log_buffer, scroll_from_bottom, result);
}

static void inspect_device(uint32_t index) {
    if (index >= device_count) return;
    usb_debug_device_t selected = devices[index];
    snprintf(status_text, sizeof(status_text), "Reading descriptors");
    if (!build_report(&selected)) {
        snprintf(status_text, sizeof(status_text), "Descriptor report incomplete");
    } else {
        snprintf(status_text, sizeof(status_text),
                 log_truncated ? "Report ready; truncated to 24 KiB" :
                                 "Report ready");
    }

    t5_ui_text_view_result_t view = {0};
    render_report(&selected, 0, &view);
    int32_t scroll = view.max_scroll_lines;
    render_report(&selected, scroll, &view);

    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui_api->poll_event(&event, 50u) ||
            event.type == T5_UI_EVENT_EXIT) return;
        bool redraw = false;
        if (event.type == T5_UI_EVENT_BACK) return;
        if (event.type == T5_UI_EVENT_PREVIOUS) {
            if (scroll < view.max_scroll_lines) ++scroll;
            redraw = true;
        } else if (event.type == T5_UI_EVENT_NEXT) {
            if (scroll > 0) --scroll;
            redraw = true;
        } else if (event.type == T5_UI_EVENT_CONFIRM) {
            (void)save_report(&selected);
            redraw = true;
        } else if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui_api->hit_test(event.touch_x, event.touch_y);
            if (hit == T5_UI_HIT_HEADER) {
                (void)save_report(&selected);
                redraw = true;
            }
        }
        if (redraw) render_report(&selected, scroll, &view);
    }
}

static bool provider_api_ready(const t5_provider_capability_api_v1 *api) {
    const size_t required = offsetof(t5_provider_capability_api_v1, release) +
                            sizeof(api->release);
    return api && api->api_version == T5_PROVIDER_CAPABILITY_API_VERSION &&
           api->struct_size >= required && api->acquire && api->release;
}

static bool storage_api_ready(const t5_storage_api_v1 *api) {
    const size_t required = offsetof(t5_storage_api_v1, write_file_atomic) +
                            sizeof(api->write_file_atomic);
    return api && api->api_version == T5_STORAGE_API_VERSION &&
           api->struct_size >= required && api->write_file_atomic;
}

static bool ui_api_ready(const t5_ui_api_v1 *api) {
    const size_t required = offsetof(t5_ui_api_v1, render_text_view) +
                            sizeof(api->render_text_view);
    return api && api->api_version == T5_UI_API_VERSION &&
           api->struct_size >= required && api->render_list &&
           api->render_text_view && api->hit_test && api->poll_event &&
           api->next_index && api->previous_index;
}

static bool acquire_host(void) {
    const void *raw = NULL;
    host_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
    if (!capability_api->acquire("usb.host", RISC_USB_HOST_API_V1,
                                 &host_lease, &raw) ||
        !host_lease || !raw) {
        char detail[USB_DEBUG_STATUS] = {0};
        const size_t required = offsetof(t5_provider_capability_api_v1, last_error) +
                                sizeof(capability_api->last_error);
        if (capability_api->struct_size >= required &&
            capability_api->last_error &&
            capability_api->last_error(detail, sizeof(detail)) && detail[0])
            snprintf(status_text, sizeof(status_text), "%s", detail);
        else
            snprintf(status_text, sizeof(status_text), "usb.host unavailable");
        host_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
        return false;
    }
    host_api = (const risc_usb_host_api_v1 *)raw;
    if (host_api->api_version != RISC_USB_HOST_API_V1 ||
        host_api->struct_size < sizeof(risc_usb_host_discovery_v1) ||
        !host_api->configuration || !host_api->claim || !host_api->release ||
        !host_api->control) {
        snprintf(status_text, sizeof(status_text),
                 "usb.host discovery ABI unavailable");
        (void)capability_api->release(host_lease);
        host_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
        host_api = NULL;
        return false;
    }
    discovery_api = (const risc_usb_host_discovery_v1 *)(const void *)host_api;
    if (!discovery_api->poll || !discovery_api->devices) {
        snprintf(status_text, sizeof(status_text),
                 "usb.host device enumeration unavailable");
        (void)capability_api->release(host_lease);
        host_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
        host_api = NULL;
        discovery_api = NULL;
        return false;
    }
    diagnostics_api = host_api->struct_size >= sizeof(risc_usb_host_diagnostics_v1) ?
        (const risc_usb_host_diagnostics_v1 *)(const void *)host_api : NULL;
    return true;
}

static void release_host(void) {
    diagnostics_api = NULL;
    discovery_api = NULL;
    host_api = NULL;
    if (host_lease && capability_api)
        (void)capability_api->release(host_lease);
    host_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
}

__attribute__((visibility("default"))) void app_main(void) {
    app_api = t5_app_get_api(T5_APP_ABI_VERSION);
    ui_api = t5_ui_get_api(T5_UI_API_VERSION);
    storage_api = t5_storage_get_api(T5_STORAGE_API_VERSION);
    capability_api = t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if (!app_api || !app_api->set_back_exits_app || !ui_api_ready(ui_api) ||
        !storage_api_ready(storage_api) || !provider_api_ready(capability_api))
        return;

    app_api->set_back_exits_app(false);
    bool running = true;
    while (running) {
        if (!host_lease && !acquire_host()) {
            device_count = 0;
            render_main(0);
            for (;;) {
                t5_ui_event_t event = {0};
                if (!ui_api->poll_event(&event, 50u) ||
                    event.type == T5_UI_EVENT_EXIT ||
                    event.type == T5_UI_EVENT_BACK) {
                    running = false;
                    break;
                }
                if (event.type == T5_UI_EVENT_CONFIRM ||
                    (event.type == T5_UI_EVENT_TAP &&
                     ui_api->hit_test(event.touch_x, event.touch_y) == T5_UI_HIT_HEADER))
                    break;
            }
            continue;
        }

        int32_t selected = 0;
        (void)refresh_devices();
        render_main(selected);
        for (;;) {
            t5_ui_event_t event = {0};
            if (!ui_api->poll_event(&event, 50u) ||
                event.type == T5_UI_EVENT_EXIT ||
                event.type == T5_UI_EVENT_BACK) {
                running = false;
                break;
            }
            bool redraw = false;
            if (event.type == T5_UI_EVENT_PREVIOUS && device_count) {
                selected = ui_api->previous_index(selected, device_count);
                redraw = true;
            } else if (event.type == T5_UI_EVENT_NEXT && device_count) {
                selected = ui_api->next_index(selected, device_count);
                redraw = true;
            } else if (event.type == T5_UI_EVENT_CONFIRM) {
                if (device_count) {
                    inspect_device((uint32_t)selected);
                    if (!refresh_devices()) {
                        release_host();
                        break;
                    }
                    if (!device_count) selected = 0;
                    else if (selected >= (int32_t)device_count) selected = (int32_t)device_count - 1;
                } else {
                    (void)refresh_devices();
                }
                redraw = true;
            } else if (event.type == T5_UI_EVENT_TAP) {
                const int32_t hit = ui_api->hit_test(event.touch_x, event.touch_y);
                if (hit >= 0 && hit < (int32_t)device_count) {
                    selected = hit;
                    inspect_device((uint32_t)selected);
                    if (!refresh_devices()) {
                        release_host();
                        break;
                    }
                    redraw = true;
                } else if (hit == T5_UI_HIT_HEADER) {
                    (void)refresh_devices();
                    redraw = true;
                }
            } else {
                size_t processed = 0;
                if (!discovery_api->poll(host_api->context, 8u, &processed)) {
                    snprintf(status_text, sizeof(status_text), "USB host poll failed");
                    release_host();
                    break;
                }
                if (processed) {
                    (void)refresh_devices();
                    if (!device_count) selected = 0;
                    else if (selected >= (int32_t)device_count) selected = (int32_t)device_count - 1;
                    redraw = true;
                }
            }
            if (redraw) render_main(selected);
        }
    }

    release_host();
    app_api->set_back_exits_app(true);
}
