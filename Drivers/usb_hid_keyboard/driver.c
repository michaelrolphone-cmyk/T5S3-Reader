#include "RiscUsbHidV1.h"

/* USB boot keyboard usage-page 0x07: modifiers, six simultaneous keys and
 * rollover handling. Pull subscriptions contain copied events, never an app
 * callback pointer. All entry points run on the serialized provider executor. */
#define KEYBOARDS 4u
typedef struct {
    uint64_t device, session;
    uint8_t iface, alt, modifiers, keys[6];
} keyboard;
typedef struct {
    uint64_t token, filter;
    risc_usb_keyboard_event_v1 queue[RISC_USB_INPUT_QUEUE_LENGTH];
    uint8_t head, count;
    bool gap;
} subscriber;
static const risc_usb_hid_api_v1 *hid;
static keyboard boards[KEYBOARDS];
static subscriber subscribers[RISC_USB_INPUT_MAX_SUBSCRIBERS];
static uint64_t sequence, token_serial;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool contains(const uint8_t *keys, uint8_t usage) {
    for (unsigned i = 0; i < 6; ++i) if (keys[i] == usage) return true;
    return false;
}
static bool emit(uint64_t device, uint8_t kind, uint8_t usage, uint8_t modifiers) {
    if (sequence == UINT64_MAX) return false;
    risc_usb_keyboard_event_v1 event = {++sequence, device, kind, usage,
                                        modifiers, 0};
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (!s->token || (s->filter && s->filter != device) || s->gap) continue;
        if (s->count == RISC_USB_INPUT_QUEUE_LENGTH) {
            s->gap = true;
            s->head = s->count = 0;
            continue;
        }
        unsigned tail = (s->head + s->count) % RISC_USB_INPUT_QUEUE_LENGTH;
        s->queue[tail] = event;
        ++s->count;
    }
    return true;
}
static bool release_board(keyboard *b) {
    if (!b->session) return true;
    /* Never leave keys logically held when USB disconnects or unloads. */
    for (unsigned i = 0; i < 6; ++i)
        if (b->keys[i] && !emit(b->device, 4, b->keys[i], 0)) return false;
    for (unsigned i = 0; i < 8; ++i)
        if ((b->modifiers & (1u << i)) &&
            !emit(b->device, 4, (uint8_t)(0xe0u + i), 0)) return false;
    if (!emit(b->device, 2, 0, 0) ||
        !hid->close(hid->context, b->session)) return false;
    *b = (keyboard){0};
    return true;
}
static keyboard *by_device(uint64_t device) {
    for (unsigned i = 0; i < KEYBOARDS; ++i)
        if (boards[i].session && boards[i].device == device) return &boards[i];
    return 0;
}
static bool update_keys(keyboard *b, const uint8_t *report, size_t n) {
    if (n != 8 || report[1] != 0) return true; /* reject partial/malformed */
    uint8_t next[6];
    for (unsigned i = 0; i < 6; ++i) {
        uint8_t key = report[i + 2];
        if (key >= 1 && key <= 3) return true; /* HID rollover: keep old state */
        if (key && contains(next, key)) return true;
        next[i] = key;
    }
    uint8_t previous = b->modifiers;
    b->modifiers = report[0];
    for (unsigned i = 0; i < 6; ++i) b->keys[i] = next[i];
    for (unsigned i = 0; i < 8; ++i)
        if ((previous & (1u << i)) && !(b->modifiers & (1u << i)) &&
            !emit(b->device, 4, (uint8_t)(0xe0u + i), b->modifiers)) return false;
    for (unsigned i = 0; i < 6; ++i)
        if (report[i + 2] && !contains(next, report[i + 2])) return false;
    /* Previous non-modifier releases are computed from a retained copy below.
     * Keep it in the caller's stack instead of sharing a transient pointer. */
    return true;
}
static bool apply_report(keyboard *b, const uint8_t *report, size_t n) {
    if (n != 8 || report[1] != 0) return true;
    uint8_t next[6] = {0};
    for (unsigned i = 0; i < 6; ++i) {
        uint8_t key = report[i + 2];
        if (key >= 1 && key <= 3) return true;
        if (key && contains(next, key)) return true;
        next[i] = key;
    }
    uint8_t old[6];
    for (unsigned i = 0; i < 6; ++i) old[i] = b->keys[i];
    uint8_t old_modifiers = b->modifiers;
    b->modifiers = report[0];
    for (unsigned i = 0; i < 6; ++i) b->keys[i] = next[i];
    for (unsigned i = 0; i < 8; ++i)
        if ((old_modifiers & (1u << i)) && !(b->modifiers & (1u << i)) &&
            !emit(b->device, 4, (uint8_t)(0xe0u + i), b->modifiers)) return false;
    for (unsigned i = 0; i < 6; ++i)
        if (old[i] && !contains(next, old[i]) &&
            !emit(b->device, 4, old[i], b->modifiers)) return false;
    for (unsigned i = 0; i < 8; ++i)
        if (!(old_modifiers & (1u << i)) && (b->modifiers & (1u << i)) &&
            !emit(b->device, 3, (uint8_t)(0xe0u + i), b->modifiers)) return false;
    for (unsigned i = 0; i < 6; ++i)
        if (next[i] && !contains(old, next[i]) &&
            !emit(b->device, 3, next[i], b->modifiers)) return false;
    return true;
}
static bool poll(void *context, size_t max_reports) {
    (void)context;
    if (!hid || max_reports == 0 || max_reports > 16 ||
        !hid->scan(hid->context, 16)) return false;
    risc_usb_hid_interface_v1 items[RISC_USB_HID_MAX_INTERFACES];
    size_t count = RISC_USB_HID_MAX_INTERFACES;
    if (!hid->interfaces(hid->context, items, &count) ||
        count > RISC_USB_HID_MAX_INTERFACES) return false;
    for (unsigned j = 0; j < KEYBOARDS; ++j) {
        keyboard *b = &boards[j];
        if (!b->session) continue;
        bool found = false;
        for (size_t i = 0; i < count; ++i)
            if (items[i].device == b->device &&
                items[i].interface_number == b->iface &&
                items[i].alternate == b->alt) found = true;
        if ((!found || !hid->present(hid->context, b->session)) &&
            !release_board(b)) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const risc_usb_hid_interface_v1 *item = &items[i];
        if (item->subclass != 1 || item->protocol != 1 ||
            by_device(item->device)) continue;
        keyboard *slot = 0;
        for (unsigned j = 0; j < KEYBOARDS; ++j)
            if (!boards[j].session) { slot = &boards[j]; break; }
        if (!slot) return false; /* explicit capacity failure, no eviction */
        uint64_t handle = hid->open(hid->context, item->device,
                                    item->interface_number, item->alternate);
        if (!handle) continue; /* another class may own the interface */
        if (!hid->set_boot_protocol(hid->context, handle, true)) {
            (void)hid->close(hid->context, handle);
            continue;
        }
        *slot = (keyboard){0};
        slot->device = item->device; slot->session = handle;
        slot->iface = item->interface_number; slot->alt = item->alternate;
        if (!emit(slot->device, 1, 0, 0)) return false;
    }
    size_t processed = 0;
    for (unsigned j = 0; j < KEYBOARDS && processed < max_reports; ++j) {
        keyboard *b = &boards[j];
        if (!b->session) continue;
        uint8_t report[RISC_USB_HID_MAX_REPORT] = {0};
        int32_t n = hid->read(hid->context, b->session, report,
                              sizeof(report), 10);
        ++processed; /* A quiet device still consumes one bounded I/O attempt. */
        if (n < 0) {
            if (!hid->present(hid->context, b->session)) {
                if (!release_board(b)) return false;
            } else return false;
        } else if (n && !apply_report(b, report, (size_t)n)) return false;
    }
    return true;
}
static uint64_t subscribe(void *context, uint64_t filter) {
    (void)context;
    if (!hid || token_serial == UINT64_MAX) return 0;
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (!subscribers[i].token) {
            subscribers[i] = (subscriber){0};
            subscribers[i].token = ++token_serial;
            subscribers[i].filter = filter;
            return subscribers[i].token;
        }
    return 0;
}
static bool unsubscribe(void *context, uint64_t token) {
    (void)context;
    if (!token) return false;
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token == token) {
            subscribers[i] = (subscriber){0}; return true;
        }
    return false;
}
static int32_t next(void *context, uint64_t token,
                    risc_usb_keyboard_event_v1 *out) {
    (void)context;
    if (!hid || !token || !out) return -1;
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (s->token != token) continue;
        if (s->gap) { s->gap = false; s->head = s->count = 0; return -1; }
        if (!s->count) return 0;
        *out = s->queue[s->head];
        s->head = (s->head + 1u) % RISC_USB_INPUT_QUEUE_LENGTH;
        --s->count;
        return 1;
    }
    return -1;
}
static bool snapshot(void *context, risc_usb_keyboard_state_v1 *out,
                     size_t *capacity) {
    (void)context;
    if (!hid || !capacity) return false;
    size_t count = 0;
    for (unsigned i = 0; i < KEYBOARDS; ++i) if (boards[i].session) ++count;
    if (*capacity < count || (count && !out)) { *capacity = count; return false; }
    size_t n = 0;
    for (unsigned i = 0; i < KEYBOARDS; ++i) if (boards[i].session) {
        risc_usb_keyboard_state_v1 state = {0};
        state.device = boards[i].device;
        state.modifiers = boards[i].modifiers;
        for (unsigned j = 0; j < 6; ++j) state.keys[j] = boards[i].keys[j];
        state.connected = 1;
        out[n++] = state;
    }
    *capacity = count;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (hid || !deps || count != 1 || !equal(deps[0].capability_id, "usb.hid") ||
        deps[0].api_version != RISC_USB_HID_API_V1 || !deps[0].api) return false;
    const risc_usb_hid_api_v1 *api = (const risc_usb_hid_api_v1 *)deps[0].api;
    if (api->api_version != RISC_USB_HID_API_V1 ||
        api->struct_size < sizeof(*api) || !api->scan || !api->interfaces ||
        !api->open || !api->set_boot_protocol || !api->read ||
        !api->present || !api->close) return false;
    hid = api;
    return true;
}
static bool quiesce(void) {
    for (unsigned i = 0; i < KEYBOARDS; ++i)
        if (boards[i].session) return false;
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) return false;
    return true;
}
static void stop(void) { if (quiesce()) hid = 0; }
static const risc_usb_keyboard_api_v1 api = {
    RISC_USB_KEYBOARD_API_V1, sizeof(risc_usb_keyboard_api_v1), 0,
    subscribe, unsubscribe, poll, next, snapshot
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-hid-keyboard", "usb.hid.keyboard", RISC_USB_KEYBOARD_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
