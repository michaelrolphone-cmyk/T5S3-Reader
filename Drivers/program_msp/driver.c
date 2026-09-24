#include "RiscProgramMspV1.h"
#include "RiscMspFetV1.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MSP_FID_VERSION 0x00u
#define MSP_FID_GET_JTAG_ID 0x0cu
#define MSP_FID_READ_MEM_WORDS_XV2 0x3du
#define MSP_FID_WRITE_FRAM_QUICK_XV2 0x4eu
#define MSP_FID_RESET_STATIC_GLOBAL_VARS 0x52u
#define PROGRAM_SESSIONS 2u

typedef struct {
    uint64_t token;
    uint64_t transport;
    uint64_t probe_device;
    uint16_t protocol;
    uint8_t probe_variant;
    uint8_t interface_mode;
    uint8_t jtag_id;
} program_session;

static const risc_msp_fet_api_v1 *msp;
static program_session sessions[PROGRAM_SESSIONS];
static uint64_t serial;
static char error_text[RISC_PROGRAM_MSP_ERROR_MAX];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void set_error(const char *text) {
    size_t i = 0;
    if (!text) text = "MSP programming failed";
    for (; i + 1u < sizeof(error_text) && text[i]; ++i) error_text[i] = text[i];
    error_text[i] = 0;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

static uint16_t parse_protocol(const uint8_t *version, size_t length) {
    if (!version || length < 4u) return 0;
    if (length >= 40u) {
        const uint16_t sw = (uint16_t)le32(version);
        const uint8_t major = (uint8_t)((sw >> 14) + 1u);
        const uint8_t minor = (uint8_t)((sw >> 8) & 0x3fu);
        return (uint16_t)(((uint16_t)major << 8) | minor);
    }
    if (length < 8u) return 0;
    const uint8_t major = (uint8_t)(version[1] >> 6);
    const uint8_t minor = (uint8_t)(version[1] & 0x3fu);
    return (uint16_t)(((uint16_t)major << 8) | minor);
}

static uint8_t map_version(const program_session *s, uint8_t fid) {
    return s && s->protocol < 0x0300u && fid > 0x11u ? (uint8_t)(fid - 1u) : fid;
}

static program_session *lookup(uint64_t token) {
    if (!msp || !token) return 0;
    for (size_t i = 0; i < PROGRAM_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}

static int32_t execute(program_session *s, uint8_t fid,
                       const uint8_t *request, size_t request_length,
                       uint8_t *response, size_t response_capacity,
                       uint32_t timeout_ms) {
    if (!s || !s->transport) return -1;
    return msp->execute(msp->context, s->transport, map_version(s, fid),
                        request, request_length, response, response_capacity,
                        timeout_ms);
}

static bool read_words(program_session *s, uint32_t address,
                       uint8_t *out, size_t length) {
    if (!s || !out || !length || (address & 1u) || (length & 1u) ||
        length > RISC_PROGRAM_MSP_MAX_CHUNK + 2u) return false;
    uint8_t request[8];
    put32(request, address);
    put32(request + 4u, (uint32_t)(length >> 1));
    const int32_t rc = execute(s, MSP_FID_READ_MEM_WORDS_XV2,
                               request, sizeof(request), out, length, 1500u);
    if (rc != (int32_t)length) {
        set_error("MSP-FET memory read failed");
        return false;
    }
    return true;
}

static bool write_words(program_session *s, uint32_t address,
                        const uint8_t *data, size_t length) {
    if (!s || !data || !length || (address & 1u) || (length & 1u) ||
        length > RISC_PROGRAM_MSP_MAX_CHUNK + 2u) return false;
    uint8_t request[8u + RISC_PROGRAM_MSP_MAX_CHUNK + 2u];
    put32(request, address);
    put32(request + 4u, (uint32_t)(length >> 1));
    memcpy(request + 8u, data, length);
    uint8_t response[8] = {0};
    const int32_t rc = execute(s, MSP_FID_WRITE_FRAM_QUICK_XV2,
                               request, 8u + length,
                               response, sizeof(response), 2500u);
    if (rc < 0) {
        set_error("Target rejected FRAM write; classic MSP flash is not supported by this provider");
        return false;
    }
    return true;
}

static int32_t write_fram(void *context, uint64_t token, uint32_t address,
                          const uint8_t *data, size_t length) {
    (void)context;
    error_text[0] = 0;
    program_session *s = lookup(token);
    if (!s || !data || !length || length > RISC_PROGRAM_MSP_MAX_CHUNK ||
        address > 0x000fffffu || length > 0x00100000u - address) {
        set_error("Invalid MSP FRAM write request");
        return -1;
    }

    const uint32_t aligned = address & ~1u;
    const uint32_t end = (uint32_t)((address + length + 1u) & ~1u);
    const size_t total = (size_t)(end - aligned);
    uint8_t words[RISC_PROGRAM_MSP_MAX_CHUNK + 2u];
    if (total > sizeof(words)) {
        set_error("MSP write exceeds provider chunk bound");
        return -1;
    }

    if (aligned != address || end != address + length) {
        if (!read_words(s, aligned, words, total)) return -1;
    } else {
        memcpy(words, data, length);
    }
    memcpy(words + (address - aligned), data, length);
    if (!write_words(s, aligned, words, total)) return -1;
    return (int32_t)length;
}

static int32_t verify_fram(void *context, uint64_t token, uint32_t address,
                           const uint8_t *data, size_t length) {
    (void)context;
    error_text[0] = 0;
    program_session *s = lookup(token);
    if (!s || !data || !length || length > RISC_PROGRAM_MSP_MAX_CHUNK ||
        address > 0x000fffffu || length > 0x00100000u - address) {
        set_error("Invalid MSP verify request");
        return -1;
    }

    const uint32_t aligned = address & ~1u;
    const uint32_t end = (uint32_t)((address + length + 1u) & ~1u);
    const size_t total = (size_t)(end - aligned);
    uint8_t words[RISC_PROGRAM_MSP_MAX_CHUNK + 2u];
    if (total > sizeof(words) || !read_words(s, aligned, words, total)) return -1;
    if (memcmp(words + (address - aligned), data, length) != 0) {
        set_error("MSP FRAM readback differs from firmware image");
        return -2;
    }
    return (int32_t)length;
}

static bool close_session(void *context, uint64_t token) {
    (void)context;
    program_session *s = lookup(token);
    if (!s) return false;
    if (!msp->close(msp->context, s->transport)) {
        set_error("MSP-FET did not quiesce");
        return false;
    }
    *s = (program_session){0};
    return true;
}

static uint64_t open_target(void *context, uint64_t requested_device,
                            uint8_t requested_mode,
                            risc_program_msp_target_v1 *target) {
    (void)context;
    error_text[0] = 0;
    if (target) memset(target, 0, sizeof(*target));
    if (!msp || !target || serial == UINT64_MAX ||
        requested_mode > RISC_PROGRAM_MSP_INTERFACE_SBW) {
        set_error("Invalid MSP programmer request");
        return 0;
    }

    if (!msp->poll(msp->context, 8u)) {
        set_error("MSP-FET discovery failed");
        return 0;
    }
    risc_msp_fet_probe_v1 probes[RISC_MSP_FET_MAX_PROBES];
    size_t count = RISC_MSP_FET_MAX_PROBES;
    if (!msp->snapshot(msp->context, probes, &count) ||
        count > RISC_MSP_FET_MAX_PROBES) {
        set_error("MSP-FET inventory unavailable");
        return 0;
    }

    const risc_msp_fet_probe_v1 *selected = 0;
    for (size_t i = 0; i < count; ++i) {
        if (requested_device && probes[i].device != requested_device) continue;
        if (selected) {
            set_error("Multiple MSP probes found; select a specific device");
            return 0;
        }
        selected = &probes[i];
    }
    if (!selected) {
        set_error("No MSP-FET/eZ-FET probe found");
        return 0;
    }

    program_session *slot = 0;
    for (size_t i = 0; i < PROGRAM_SESSIONS; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) {
        set_error("MSP programmer session limit reached");
        return 0;
    }

    uint8_t modes[2] = {RISC_MSP_FET_INTERFACE_SBW, RISC_MSP_FET_INTERFACE_JTAG};
    size_t mode_count = 2u;
    if (requested_mode == RISC_PROGRAM_MSP_INTERFACE_JTAG) {
        modes[0] = RISC_MSP_FET_INTERFACE_JTAG; mode_count = 1u;
    } else if (requested_mode == RISC_PROGRAM_MSP_INTERFACE_SBW) {
        modes[0] = RISC_MSP_FET_INTERFACE_SBW; mode_count = 1u;
    }

    uint64_t transport = 0;
    uint8_t opened_mode = 0;
    for (size_t i = 0; i < mode_count && !transport; ++i) {
        transport = msp->open(msp->context, selected->device, modes[i]);
        if (transport) opened_mode = modes[i];
    }
    if (!transport) {
        set_error("MSP target did not enter JTAG/Spy-Bi-Wire debug mode");
        return 0;
    }

    uint8_t version[64] = {0};
    const uint8_t version_request = 0u;
    const int32_t version_length = msp->execute(
        msp->context, transport, MSP_FID_VERSION,
        &version_request, sizeof(version_request), version, sizeof(version), 1000u);
    const uint16_t protocol = version_length > 0 ?
        parse_protocol(version, (size_t)version_length) : 0u;
    if (!protocol) {
        (void)msp->close(msp->context, transport);
        set_error("MSP-FET protocol version is unsupported");
        return 0;
    }

    program_session candidate = {0};
    candidate.transport = transport;
    candidate.probe_device = selected->device;
    candidate.protocol = protocol;
    candidate.probe_variant = selected->variant;
    candidate.interface_mode = opened_mode;

    uint8_t ignored[16] = {0};
    if (execute(&candidate, MSP_FID_RESET_STATIC_GLOBAL_VARS,
                0, 0, ignored, sizeof(ignored), 1000u) < 0) {
        (void)msp->close(msp->context, transport);
        set_error("MSP-FET HAL reset failed");
        return 0;
    }

    uint8_t identity[16] = {0};
    const int32_t id_length = execute(&candidate, MSP_FID_GET_JTAG_ID,
                                      0, 0, identity, sizeof(identity), 1000u);
    if (id_length < 1) {
        (void)msp->close(msp->context, transport);
        set_error("Cannot identify MSP target");
        return 0;
    }
    const uint8_t jtag_id = identity[0];
    if (jtag_id != 0x91u && jtag_id != 0x95u && jtag_id != 0x99u) {
        (void)msp->close(msp->context, transport);
        set_error("MSP target is not an XV2 device supported by the FRAM programmer");
        return 0;
    }

    uint64_t token = ++serial;
    if (!token) token = ++serial;
    candidate.token = token;
    candidate.jtag_id = jtag_id;
    *slot = candidate;

    target->probe_device = candidate.probe_device;
    target->probe_variant = candidate.probe_variant;
    target->interface_mode = candidate.interface_mode;
    target->jtag_id = candidate.jtag_id;
    target->protocol_major = (uint8_t)(candidate.protocol >> 8);
    target->protocol_minor = (uint8_t)candidate.protocol;
    return token;
}

static bool last_error_api(void *context, char *out, size_t capacity) {
    (void)context;
    if (!out || !capacity || !error_text[0]) return false;
    size_t i = 0;
    for (; i + 1u < capacity && error_text[i]; ++i) out[i] = error_text[i];
    out[i] = 0;
    return true;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (msp || !deps || count != 1u ||
        !equal(deps[0].capability_id, "debug.vendor.msp") ||
        deps[0].api_version != RISC_MSP_FET_API_V1 || !deps[0].api)
        return false;
    const risc_msp_fet_api_v1 *api = (const risc_msp_fet_api_v1 *)deps[0].api;
    if (api->api_version != RISC_MSP_FET_API_V1 ||
        api->struct_size < sizeof(*api) || !api->poll || !api->snapshot ||
        !api->open || !api->execute || !api->close)
        return false;
    msp = api;
    error_text[0] = 0;
    return true;
}

static bool quiesce(void) {
    for (size_t i = 0; i < PROGRAM_SESSIONS; ++i)
        if (sessions[i].token) return false;
    return true;
}

static void stop(void) {
    if (!quiesce()) return;
    msp = 0;
    error_text[0] = 0;
}

static const risc_program_msp_api_v1 capability = {
    RISC_PROGRAM_MSP_API_V1, sizeof(risc_program_msp_api_v1), 0,
    open_target, write_fram, verify_fram, close_session, last_error_api
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "program-msp", "program.msp", RISC_PROGRAM_MSP_API_V1,
    &capability, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
