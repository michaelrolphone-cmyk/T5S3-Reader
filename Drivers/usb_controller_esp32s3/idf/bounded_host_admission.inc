/* SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Private serialized-owner admission. Fixed pool preparation is explicit and
 * unqualified; all subsequent steps use fixed work and try-only SDK locks. */
#include "BoundedAdmission.h"
#include <string.h>
extern esp_err_t risc_hcd_pool_prepare(void);
extern esp_err_t risc_hcd_pool_dispose(void);
extern size_t risc_hcd_pool_internal_bytes(void);
extern size_t risc_hcd_pool_dma_bytes(void);
extern esp_err_t risc_usbh_configuration_copy(usb_device_handle_t, uint8_t *, size_t,
                                              size_t *, uint16_t *, uint16_t *, uint8_t *);
extern esp_err_t risc_usbh_pipe_allocate(usb_device_handle_t, const usbh_ep_config_t *, hcd_pipe_handle_t *);
extern esp_err_t risc_usbh_pipe_release(usb_device_handle_t, uint8_t, hcd_pipe_handle_t);
extern esp_err_t risc_usbh_device_close_try(usb_device_handle_t, uint8_t *, bool *);
extern esp_err_t risc_usbh_device_address_try(usb_device_handle_t, uint8_t *);

enum { RISC_NATIVE_CLAIMS = 16, RISC_NATIVE_ENDPOINTS = 8, RISC_NATIVE_CONFIG = 4096 };
typedef struct {
    endpoint_t endpoint;
    uint64_t owner;
    uint32_t cookie;
    bool used;
} risc_native_endpoint;
typedef struct {
    uint64_t token;
    client_t *client;
    usb_device_handle_t device;
    interface_t *interface;
    usb_intf_desc_t descriptor;
    usb_ep_desc_t endpoints[RISC_NATIVE_ENDPOINTS];
    size_t length, cursor;
    uint32_t endpoint_map;
    uint8_t number, alternate, found, count, acquired, phase;
    bool selected, published, retained;
    esp_err_t failed;
} risc_native_claim;
enum { RISC_CLAIM_SNAPSHOT, RISC_CLAIM_PARSE, RISC_CLAIM_ACQUIRE, RISC_CLAIM_COMMIT, RISC_CLAIM_READY };
static risc_native_claim *risc_native_claims;
static risc_native_endpoint *risc_native_endpoints;
static uint8_t *risc_native_interfaces, *risc_native_configuration;
static uint64_t risc_native_active;
static uint32_t risc_native_cookie_serial; /* Never reset during this loaded image. */
static volatile bool risc_native_callback_fault;
#define RISC_NATIVE_INTERFACE_STRIDE (sizeof(interface_t) + RISC_NATIVE_ENDPOINTS * sizeof(endpoint_t *))
_Static_assert(RISC_NATIVE_INTERFACE_STRIDE % _Alignof(interface_t) == 0, "Interface pool alignment");
__attribute__((used)) static const size_t risc_native_pool_cost[] = {
    RISC_NATIVE_CLAIMS * (sizeof(risc_native_claim) + RISC_NATIVE_INTERFACE_STRIDE) +
        RISC_NATIVE_ENDPOINTS * sizeof(risc_native_endpoint) + RISC_NATIVE_CONFIG
};

size_t risc_usb_admission_internal_bytes(void)
{
    return RISC_NATIVE_CLAIMS * (sizeof(risc_native_claim) + RISC_NATIVE_INTERFACE_STRIDE) +
        RISC_NATIVE_ENDPOINTS * sizeof(risc_native_endpoint) + RISC_NATIVE_CONFIG +
        risc_hcd_pool_internal_bytes();
}
size_t risc_usb_admission_dma_bytes(void) { return risc_hcd_pool_dma_bytes(); }
bool risc_usb_admission_faulted(void) { return risc_native_callback_fault; }
bool risc_usb_admission_owns_resources(void)
{
    if (risc_native_callback_fault || risc_native_active) return true;
    if (risc_native_claims) for (unsigned i = 0; i < RISC_NATIVE_CLAIMS; ++i)
        if (risc_native_claims[i].token) return true;
    return false;
}
static risc_native_claim *risc_native_find(uint64_t token)
{
    if (!token || !risc_native_claims) return NULL;
    for (unsigned i = 0; i < RISC_NATIVE_CLAIMS; ++i)
        if (risc_native_claims[i].token == token) return &risc_native_claims[i];
    return NULL;
}
static bool risc_native_interface_owned(interface_t *interface)
{
    if (risc_native_claims) for (unsigned i = 0; i < RISC_NATIVE_CLAIMS; ++i)
        if (risc_native_claims[i].token && risc_native_claims[i].interface == interface) return true;
    return false;
}

esp_err_t risc_usb_admission_prepare(void)
{
    if (risc_native_callback_fault) return ESP_ERR_INVALID_STATE;
    if (risc_native_claims) return ESP_OK;
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    risc_native_claim *claims = heap_caps_calloc(RISC_NATIVE_CLAIMS, sizeof(*claims), caps);
    risc_native_endpoint *endpoints = heap_caps_calloc(RISC_NATIVE_ENDPOINTS, sizeof(*endpoints), caps);
    uint8_t *interfaces = heap_caps_calloc(RISC_NATIVE_CLAIMS, RISC_NATIVE_INTERFACE_STRIDE, caps);
    uint8_t *configuration = heap_caps_calloc(1, RISC_NATIVE_CONFIG, caps);
    esp_err_t ret = ESP_ERR_NO_MEM;
    if (!claims || !endpoints || !interfaces || !configuration) goto failure;
    ret = risc_hcd_pool_prepare();
    if (ret != ESP_OK) goto failure;
    risc_native_claims = claims; risc_native_endpoints = endpoints;
    risc_native_interfaces = interfaces; risc_native_configuration = configuration;
    return ESP_OK;
failure:
    heap_caps_free(configuration); heap_caps_free(interfaces);
    heap_caps_free(endpoints); heap_caps_free(claims);
    return ret;
}

esp_err_t risc_usb_admission_dispose(void)
{
    if (risc_usb_admission_owns_resources()) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = risc_hcd_pool_dispose();
    if (ret != ESP_OK) return ret;
    heap_caps_free(risc_native_configuration); heap_caps_free(risc_native_interfaces);
    heap_caps_free(risc_native_endpoints); heap_caps_free(risc_native_claims);
    risc_native_configuration = risc_native_interfaces = NULL;
    risc_native_endpoints = NULL; risc_native_claims = NULL;
    return ESP_OK;
}

static bool risc_native_pipe_callback(hcd_pipe_handle_t pipe, hcd_pipe_event_t event,
                                      void *argument, bool isr)
{
    const uint32_t cookie = (uint32_t)(uintptr_t)argument;
    const unsigned index = cookie & 7u;
    HOST_ENTER_CRITICAL_SAFE();
    risc_native_endpoint *slot = risc_native_endpoints ? &risc_native_endpoints[index] : NULL;
    if (!cookie || !slot || !slot->used || slot->cookie != cookie ||
        slot->endpoint.constant.pipe_hdl != pipe) {
        risc_native_callback_fault = true;
        HOST_EXIT_CRITICAL_SAFE(); return false;
    }
    bool yield = pipe_callback(pipe, event, &slot->endpoint, isr);
    HOST_EXIT_CRITICAL_SAFE(); return yield;
}

esp_err_t risc_usb_configuration_copy(usb_host_client_handle_t handle, usb_device_handle_t device,
    uint8_t *out, size_t capacity, size_t *length, uint16_t *vid, uint16_t *pid)
{
    if (!handle || !device || !out || !length || !vid || !pid) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    client_t *client = (client_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    uint8_t address = 0;
    if (!p_host_lib_obj || p_host_lib_obj->dynamic.flags.num_clients != 1 ||
        risc_native_callback_fault) goto exit;
    ret = risc_usbh_device_address_try(device, &address);
    if (ret != ESP_OK) goto exit;
    if (address == 0 || address > 32 || !_check_client_opened_device(client, address)) {
        ret = ESP_ERR_INVALID_STATE; goto exit;
    }
    ret = risc_usbh_configuration_copy(device, out, capacity, length, vid, pid, &address);
exit:
    HOST_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usb_claim_begin(usb_host_client_handle_t handle, usb_device_handle_t device,
                              uint8_t number, uint8_t alternate, uint64_t token)
{
    if (!handle || !device || !token) return ESP_ERR_INVALID_ARG;
    if (!risc_native_claims || risc_native_callback_fault) return ESP_ERR_INVALID_STATE;
    if (risc_native_active || risc_native_find(token)) return ESP_ERR_NOT_FINISHED;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    client_t *client = (client_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (!p_host_lib_obj || p_host_lib_obj->dynamic.flags.num_clients != 1 ||
        client->dynamic.flags.taking_mux || client->dynamic.flags.handling_events ||
        client->dynamic.flags.blocked) goto exit;
    unsigned visited = 0;
    interface_t *existing;
    TAILQ_FOREACH(existing, &client->mux_protected.interface_tailq, mux_protected.tailq_entry) {
        if (++visited > RISC_NATIVE_CLAIMS) goto exit;
        if (existing->constant.dev_hdl == device &&
            existing->constant.intf_desc->bInterfaceNumber == number) goto exit;
    }
    risc_native_claim *free_slot = NULL;
    unsigned index = 0;
    for (unsigned i = 0; i < RISC_NATIVE_CLAIMS; ++i) {
        risc_native_claim *claim = &risc_native_claims[i];
        if (claim->token && claim->device == device && claim->number == number) goto exit;
        if (!claim->token && !free_slot) { free_slot = claim; index = i; }
    }
    if (!free_slot) { ret = ESP_ERR_NO_MEM; goto exit; }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->token = token; free_slot->client = client; free_slot->device = device;
    free_slot->number = number; free_slot->alternate = alternate;
    free_slot->interface = (interface_t *)(risc_native_interfaces + index * RISC_NATIVE_INTERFACE_STRIDE);
    memset(free_slot->interface, 0, RISC_NATIVE_INTERFACE_STRIDE);
    risc_native_active = token; ret = ESP_OK;
exit:
    HOST_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usb_claim_step(uint64_t token)
{
    risc_native_claim *claim = risc_native_find(token);
    if (!claim || claim->retained || risc_native_callback_fault) return ESP_ERR_INVALID_STATE;
    if (claim->published) return ESP_OK;
    if (risc_native_active != token) return ESP_ERR_INVALID_STATE;
    if (claim->failed) return claim->failed;
    esp_err_t ready = risc_usb_host_library_idle((usb_host_client_handle_t)claim->client);
    if (ready != ESP_OK) return ready;
    if (claim->phase == RISC_CLAIM_SNAPSHOT) {
        uint16_t vid, pid;
        esp_err_t ret = risc_usb_configuration_copy((usb_host_client_handle_t)claim->client, claim->device,
            risc_native_configuration, RISC_NATIVE_CONFIG, &claim->length, &vid, &pid);
        if (ret != ESP_OK) { if (ret != ESP_ERR_NOT_FINISHED) claim->failed = ret; return ret; }
        if (risc_native_configuration[0] < 9 || risc_native_configuration[1] != 2) {
            claim->failed = ESP_ERR_INVALID_ARG; return claim->failed;
        }
        claim->phase = RISC_CLAIM_PARSE; return ESP_ERR_NOT_FINISHED;
    }
    if (claim->phase == RISC_CLAIM_PARSE) {
        if (claim->cursor == claim->length) {
            if (!claim->found || claim->count != claim->descriptor.bNumEndpoints) {
                claim->failed = ESP_ERR_NOT_FOUND; return claim->failed;
            }
            interface_t *intf = claim->interface;
            intf->constant.client_obj = claim->client; intf->constant.dev_hdl = claim->device;
            intf->constant.intf_desc = &claim->descriptor;
            claim->phase = RISC_CLAIM_ACQUIRE; return ESP_ERR_NOT_FINISHED;
        }
        const uint8_t *record = risc_native_configuration + claim->cursor;
        size_t available = claim->length - claim->cursor;
        if (available < 2 || record[0] < 2 || record[0] > available) {
            claim->failed = ESP_ERR_INVALID_ARG; return claim->failed;
        }
        if (record[1] == 4) {
            if (record[0] < sizeof(usb_intf_desc_t)) { claim->failed = ESP_ERR_INVALID_ARG; return claim->failed; }
            claim->selected = record[2] == claim->number && record[3] == claim->alternate;
            if (claim->selected) {
                if (claim->found || record[4] > RISC_NATIVE_ENDPOINTS) {
                    claim->failed = ESP_ERR_NOT_SUPPORTED; return claim->failed;
                }
                memcpy(&claim->descriptor, record, sizeof(claim->descriptor)); claim->found = 1;
            }
        } else if (record[1] == 5 && claim->selected) {
            if (record[0] < sizeof(usb_ep_desc_t) || claim->count >= RISC_NATIVE_ENDPOINTS) {
                claim->failed = ESP_ERR_INVALID_ARG; return claim->failed;
            }
            const unsigned number = record[2] & 15u;
            const unsigned bit = number + ((record[2] & 0x80u) ? 16u : 0u);
            if (!number || (record[2] & 0x70u) || (claim->endpoint_map & (1u << bit)) ||
                ((record[3] & 3u) != USB_TRANSFER_TYPE_BULK && (record[3] & 3u) != USB_TRANSFER_TYPE_INTR)) {
                claim->failed = ESP_ERR_NOT_SUPPORTED; return claim->failed;
            }
            claim->endpoint_map |= 1u << bit;
            memcpy(&claim->endpoints[claim->count++], record, sizeof(usb_ep_desc_t));
        }
        claim->cursor += record[0]; return ESP_ERR_NOT_FINISHED;
    }
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    esp_err_t ret = ESP_ERR_NOT_FINISHED;
    if (claim->phase == RISC_CLAIM_ACQUIRE) {
        if (claim->acquired == claim->count) { claim->phase = RISC_CLAIM_COMMIT; goto exit; }
        risc_native_endpoint *slot = NULL;
        unsigned index = 0;
        for (; index < RISC_NATIVE_ENDPOINTS; ++index)
            if (!risc_native_endpoints[index].used) { slot = &risc_native_endpoints[index]; break; }
        if (!slot || risc_native_cookie_serial == (UINT32_MAX >> 3)) { ret = ESP_ERR_NO_MEM; goto failed; }
        memset(slot, 0, sizeof(*slot));
        slot->cookie = (++risc_native_cookie_serial << 3) | index;
        endpoint_t *ep = &slot->endpoint;
        ep->constant.intf_obj = claim->interface;
        ep->constant.ep_desc = &claim->endpoints[claim->acquired];
        ep->dynamic.flags.bounded_owned = 1;
        usbh_ep_config_t config = {.ep_desc = ep->constant.ep_desc, .pipe_cb = risc_native_pipe_callback,
            .pipe_cb_arg = (void *)(uintptr_t)slot->cookie, .context = ep};
        hcd_pipe_handle_t pipe = NULL;
        ret = risc_usbh_pipe_allocate(claim->device, &config, &pipe);
        if (ret != ESP_OK) { if (ret != ESP_ERR_NOT_FINISHED) goto failed; goto exit; }
        ep->constant.pipe_hdl = pipe; slot->owner = token; slot->used = true;
        claim->interface->constant.endpoints[claim->acquired++] = ep;
        ret = ESP_ERR_NOT_FINISHED;
    } else if (claim->phase == RISC_CLAIM_COMMIT) {
        TAILQ_INSERT_TAIL(&claim->client->mux_protected.interface_tailq, claim->interface, mux_protected.tailq_entry);
        for (unsigned i = 0; i < claim->count; ++i)
            TAILQ_INSERT_TAIL(&claim->client->dynamic.idle_ep_tailq,
                              claim->interface->constant.endpoints[i], dynamic.tailq_entry);
        claim->client->dynamic.flags.num_intf_claimed++;
        claim->published = true; claim->phase = RISC_CLAIM_READY;
        risc_native_active = 0; ret = ESP_OK;
    }
    goto exit;
failed:
    claim->failed = ret;
exit:
    HOST_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usb_claim_release_step(uint64_t token)
{
    risc_native_claim *claim = risc_native_find(token);
    if (!claim || claim->retained || risc_native_callback_fault) return ESP_ERR_INVALID_STATE;
    if (risc_native_active && risc_native_active != token) return ESP_ERR_NOT_FINISHED;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    risc_native_active = token;
    esp_err_t ret = ESP_ERR_NOT_FINISHED;
    if (claim->acquired) {
        endpoint_t *ep = claim->interface->constant.endpoints[claim->acquired - 1];
        if (ep->dynamic.num_urb_inflight || ep->dynamic.flags.pending) { ret = ESP_ERR_INVALID_STATE; goto exit; }
        ret = risc_usbh_pipe_release(claim->device, ep->constant.ep_desc->bEndpointAddress, ep->constant.pipe_hdl);
        if (ret != ESP_OK) goto exit;
        if (claim->published) TAILQ_REMOVE(&claim->client->dynamic.idle_ep_tailq, ep, dynamic.tailq_entry);
        risc_native_endpoint *slot = __containerof(ep, risc_native_endpoint, endpoint);
        slot->used = false; slot->owner = 0;
        claim->interface->constant.endpoints[--claim->acquired] = NULL;
        ret = ESP_ERR_NOT_FINISHED;
    } else {
        if (claim->published) {
            TAILQ_REMOVE(&claim->client->mux_protected.interface_tailq, claim->interface, mux_protected.tailq_entry);
            claim->client->dynamic.flags.num_intf_claimed--;
        }
        memset(claim->interface, 0, RISC_NATIVE_INTERFACE_STRIDE);
        memset(claim, 0, sizeof(*claim)); risc_native_active = 0;
        ret = ESP_OK;
    }
exit:
    HOST_EXIT_CRITICAL(); return ret;
}

void risc_usb_claim_retain(uint64_t token)
{
    risc_native_claim *claim = risc_native_find(token);
    if (claim) claim->retained = true;
}
bool risc_usb_claim_abandon_unacquired(uint64_t token)
{
    risc_native_claim *claim = risc_native_find(token);
    if (!claim) return true;
    if (claim->retained || risc_native_callback_fault || claim->acquired || claim->published)
        return false;
    /* Only this serialized owner can access an unpublished claim with no
     * physical endpoints. No lock, allocator or callback is reached here. */
    memset(claim->interface, 0, RISC_NATIVE_INTERFACE_STRIDE);
    memset(claim, 0, sizeof(*claim));
    if (risc_native_active == token) risc_native_active = 0;
    return true;
}
bool risc_usb_claim_state_copy(uint64_t token, risc_usb_claim_state *out)
{
    risc_native_claim *claim = risc_native_find(token);
    if (!claim || !out) return false;
    *out = (risc_usb_claim_state){claim->token, claim->count, claim->acquired,
                                claim->published, claim->retained || risc_native_callback_fault};
    return true;
}

esp_err_t risc_usb_device_close_try(usb_host_client_handle_t handle, usb_device_handle_t device,
                                   bool *reference_closed, bool *deferred)
{
    if (!handle || !device || !reference_closed || !deferred) return ESP_ERR_INVALID_ARG;
    *reference_closed = false; *deferred = false;
    if (risc_native_callback_fault || risc_native_active) return ESP_ERR_INVALID_STATE;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    client_t *client = (client_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (!p_host_lib_obj || p_host_lib_obj->dynamic.flags.num_clients != 1) goto exit;
    unsigned visited = 0;
    interface_t *intf;
    TAILQ_FOREACH(intf, &client->mux_protected.interface_tailq, mux_protected.tailq_entry) {
        if (++visited > RISC_NATIVE_CLAIMS || intf->constant.dev_hdl == device) goto exit;
    }
    if (risc_native_claims) for (unsigned i = 0; i < RISC_NATIVE_CLAIMS; ++i)
        if (risc_native_claims[i].token && risc_native_claims[i].device == device) goto exit;
    /* Address is immutable while the client owns this live device reference.
     * Validate opening before the USBH decrement, not after it. */
    uint8_t address = 0;
    ret = risc_usbh_device_address_try(device, &address);
    if (ret != ESP_OK) goto exit;
    if (!address || address > 32 || !_check_client_opened_device(client, address)) { ret = ESP_ERR_NOT_FOUND; goto exit; }
    ret = risc_usbh_device_close_try(device, &address, deferred);
    if (ret != ESP_OK) goto exit;
    _clear_client_opened_device(client, address); *reference_closed = true;
    if (*deferred) {
        p_host_lib_obj->dynamic.process_pending_flags |= PROCESS_PENDING_FLAG_USBH;
        p_host_lib_obj->dynamic.flags.process_pending = 1;
    }
exit:
    HOST_EXIT_CRITICAL(); return ret;
}
