#include "RiscUsbHostDeadlinesV1.h"
#include "RiscStreamSessionProviderV1.h"
#include "../../sdk/app/RiscSerialStreamSessionV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Execute the actual separately compiled class ELF. Simulated host and queue
 * callbacks enforce ordering and exact physical/queue custody independently. */
#if VENDOR == 1
#define VENDOR_NAME "CH34x"
#define OPEN_CONTROLS 4
#define OPEN_EVENTS "SDACCCC"
#elif VENDOR == 2
#define VENDOR_NAME "CP210x"
#define OPEN_CONTROLS 3
#define OPEN_EVENTS "SDACCC"
#elif VENDOR == 3
#define VENDOR_NAME "FTDI"
#define OPEN_CONTROLS 7
#define OPEN_EVENTS "SDACCCCCCC"
#else
#error Select the vendor under test
#endif
static uint8_t descriptor[] = {9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,0xff,0,0,0,
    7,5,0x81,2,64,0,0,7,5,0x02,2,64,0,0};
static uint16_t device_pid = VENDOR == 1 ? 0x7523 : VENDOR == 2 ? 0xea60 : 0x6001;
static uint16_t device_bcd __attribute__((unused)) = 0x0600;
static uint8_t device_version = 0x30;
static uint8_t wanted_bits = 8, wanted_parity = 0, wanted_stops = 1;
static bool malformed_descriptor;
static void *mutate_request; static size_t mutate_size;
static int control_result, bulk_result, refuse_finish;
static bool bulk_overrun, malformed_publish, duplicate_publish, lower_retained, retain_write;
static uint8_t requests[64]; static uint16_t values[64], indexes[64];
static unsigned rx_mode;
static uint64_t tick, device = 2;
static uint32_t charge = 10, max_produce = 2, max_write = 2;
static int snapshots, configs, claims, controls, releases, publishes, closes, reads, writes, finishes;
static int fail_claim, fail_release, fail_publish, fail_close, snapshot_mode, fail_control;
static bool over_time, failed_claim_token, claim_retained, physical[1], queue_live[2];
static bool rx_sent, tx_consumed, forbid_io, configured, error_read;
static uint32_t seen_ms[64]; static char events[128]; static size_t calls;
static uint8_t received[1024], sent[1024]; static size_t received_size, sent_size;
static uint32_t queue_ids[2]; static int32_t terminal[2];
static void step(char event, uint32_t ms) {
    assert(calls < sizeof(seen_ms) / sizeof(seen_ms[0]) && ms && ms <= 1000);
    events[calls] = event; seen_ms[calls++] = ms;
    tick += over_time ? ms + 1 : charge;
}
static uint64_t now_ms(void *ctx) { assert(ctx == (void *)1 && !lower_retained); return tick; }
static int32_t snapshot(void *ctx, uint64_t *out, size_t *count, uint32_t ms) {
    assert(ctx == (void *)1 && *count == 8); ++snapshots; step('S', ms);
    if (mutate_request) { memset(mutate_request, 0, mutate_size); mutate_request = 0; }
    if (snapshot_mode == 1) return RISC_STREAM_IO;
    if (snapshot_mode == 2) { out[0] = 2; out[1] = 2; *count = 2; return 0; }
    if (snapshot_mode == 3) { out[0] = 0; *count = 1; return 0; }
    if (snapshot_mode == 4) { *count = 9; return 0; }
    *count = device ? 1 : 0; out[0] = device; return 0;
}
static bool configuration(void *ctx, uint64_t token, uint8_t *out, size_t *len,
                          uint16_t *vid, uint16_t *pid) {
    assert(ctx == (void *)1 && token == device && *len >= sizeof(descriptor));
    memcpy(out, descriptor, sizeof(descriptor)); *len = sizeof(descriptor);
    *vid = VENDOR == 1 ? 0x1a86 : VENDOR == 2 ? 0x10c4 : 0x0403; *pid = device_pid;
    if (malformed_descriptor) --*len;
    return true;
}
static int32_t configuration_timed(void *ctx, uint64_t token, uint8_t *out, size_t *len,
                                   uint16_t *vid, uint16_t *pid, uint32_t ms) {
    ++configs; step('D', ms); return configuration(ctx, token, out, len, vid, pid) ? 0 : -5;
}
static bool claim(void *ctx, uint64_t token, uint8_t iface, uint8_t alt, uint64_t *out) {
    assert(ctx == (void *)1 && token == device && iface == 0 && !alt && !physical[iface]);
    physical[iface] = true; *out = 100 + iface; return true;
}
static int32_t claim_timed(void *ctx, uint64_t token, uint8_t iface, uint8_t alt,
                           uint64_t *out, uint32_t ms) {
    ++claims; step(iface ? 'B' : 'A', ms);
    if (claims == fail_claim) {
        if (failed_claim_token) claim(ctx, token, iface, alt, out);
        return claim_retained ? RISC_STREAM_RETAINED : RISC_STREAM_IO;
    }
    return claim(ctx, token, iface, alt, out) ? 0 : -5;
}
static bool release_checked(void *ctx, uint64_t token) {
    assert(ctx == (void *)1 && token >= 100 && token == 100 && physical[token - 100]);
    if (releases == fail_release) return false;
    physical[token - 100] = false; return true;
}
static void release_legacy(void *ctx, uint64_t token) { ++releases; (void)release_checked(ctx, token); }
static int32_t release_timed(void *ctx, uint64_t token, uint32_t ms) {
    ++releases; step(token == 100 ? 'b' : 'a', ms); return release_checked(ctx, token) ? 0 : -5;
}
static int32_t control(void *ctx, uint64_t token, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t iface, uint8_t *payload, uint16_t n, uint32_t ms) {
    assert(ctx == (void *)1 && token == 100 && physical[0]);
    assert(controls < 64); requests[controls] = request; values[controls] = value; indexes[controls] = iface;
    ++controls; step('C', ms);
#if VENDOR == 1
    if (request == 0x5f) { assert(type == 0xc0 && !value && !iface && n == 2); payload[0] = device_version; payload[1] = 0; }
    else {
        assert(type == 0x40 && !n && !payload);
        if (request == 0xa1) assert(!value && !iface);
        else if (request == 0x9a && value == 0x1312) assert(iface == (device_version > 0x27 ? 0xcc83 : 0xcc03));
        else if (request == 0x9a) {
            assert(value == 0x2518 && iface == (wanted_bits == 8 ? 0xc3 : 0xde)); configured = true;
        } else assert(request == 0xa4 && value == 0xff9f && !iface);
        if (request == 0x9a && device_version < 0x30) configured = true;
    }
#elif VENDOR == 2
    assert(type == 0x41 && !iface);
    if (request == 0) assert((value == 0 || value == 1) && !n && !payload);
    else if (request == 0x1e) {
        assert(!value && n == 4 && payload && payload[0] == 0 && payload[1] == 0xc2 && payload[2] == 1 && payload[3] == 0);
    } else if (request == 3) {
        assert(!n && !payload && value == (wanted_bits == 8 ? 0x0800 : 0x0722)); configured = true;
    } else assert(request == 7 && value == 0x0303 && !n && !payload);
#else
    if (request == 6) {
        assert(type == 0x80 && value == 0x0100 && !iface && n == 18);
        memset(payload, 0, n); payload[0] = 18; payload[1] = 1; payload[12] = (uint8_t)device_bcd; payload[13] = device_bcd >> 8;
    } else {
        assert(type == 0x40 && !n && !payload);
        if (request == 0) assert(value <= 2 && iface == (device_bcd == 0x0600 ? 0 : 1));
        else if (request == 2) assert(!value && iface == (device_bcd == 0x0600 ? 0 : 1));
        else if (request == 4) assert(value == (wanted_bits == 8 ? 0x0008 : 0x1207));
        else if (request == 3) {
            assert(value == (device_bcd == 0x0900 ? 0 : 0x001a));
            assert(iface == (device_bcd == 0x0900 ? 0x0201 : device_bcd == 0x1000 ? 1 : 0)); configured = true;
        } else assert(request == 1 && (value == 0x0303 || value == 0x0300));
    }
#endif
    if (control_result && controls == fail_control) { lower_retained = control_result == RISC_STREAM_RETAINED; return control_result; }
    return controls == fail_control ? (n ? n - 1 : -1) : n;
}
static int32_t bulk_read(void *ctx, uint64_t token, uint8_t ep, uint8_t *dst, size_t n, uint32_t ms) {
    assert(ctx == (void *)1 && token == 100 && ep == 0x81 && (n == (VENDOR == 3 ? ((size_t)descriptor[22] | ((size_t)descriptor[23] << 8)) : 256)) && physical[0] && !forbid_io);
    ++reads; assert(ms == 1); if (bulk_overrun) tick += 2; if (bulk_result && !retain_write) { lower_retained = bulk_result == RISC_STREAM_RETAINED; return bulk_result; } if (error_read) return -1; if (rx_sent) return 0;
    rx_sent = true;
    if (VENDOR == 3) {
        dst[0] = 0x01; dst[1] = 0x60;
        if (rx_mode == 1) return 2; /* A valid status-only packet. */
        if (rx_mode == 2) return 1; /* A malformed partial status prefix. */
        if (rx_mode == 3) { for (size_t i = 2; i < n; ++i) dst[i] = (uint8_t)(i - 2); return (int32_t)n; }
        memcpy(dst + 2, "abcde", 5); return 7;
    }
    memcpy(dst, "abcde", 5); return 5;
}
static int32_t bulk_write(void *ctx, uint64_t token, uint8_t ep, const uint8_t *src, size_t n, uint32_t ms) {
    assert(ctx == (void *)1 && token == 100 && ep == 2 && physical[0] && !forbid_io);
    ++writes; assert(ms == 1); if (bulk_result) { lower_retained = bulk_result == RISC_STREAM_RETAINED; return bulk_result; } if (n > max_write) n = max_write;
    memcpy(sent + sent_size, src, n); sent_size += n; return (int32_t)n;
}
static bool poll(void *ctx, size_t maximum, size_t *done) { (void)ctx; (void)maximum; *done = 0; return true; }
static bool devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx; assert(*count >= 1); out[0] = device; *count = device ? 1 : 0; return true;
}
static int32_t publish(uint64_t ctx, const risc_stream_endpoint_v1 *spec, uint32_t *out) {
    assert(ctx == 9 && configured && physical[0] && spec->kind == 1 && spec->byte_capacity == 2048);
    ++publishes;
    if (publishes == fail_publish) {
        if (malformed_publish) { queue_live[0] = true; queue_ids[0] = *out = 1234; }
        return RISC_STREAM_BUSY;
    }
    if (duplicate_publish && publishes == 2) { *out = queue_ids[0]; return 0; }
    unsigned slot = spec->rights == RISC_STREAM_READ ? 0 : 1;
    assert(spec->rights == (slot ? RISC_STREAM_WRITE : RISC_STREAM_READ) && !queue_live[slot]);
    queue_live[slot] = true; queue_ids[slot] = (uint32_t)(1000 + publishes); *out = queue_ids[slot]; return 0;
}
static int qslot(uint32_t id) { if (id == queue_ids[0]) return 0; assert(id == queue_ids[1]); return 1; }
static int32_t produce(uint64_t ctx, uint32_t id, const void *src, uint32_t n, uint32_t *actual) {
    assert(ctx == 9 && !lower_retained && qslot(id) == 0 && queue_live[0]);
    if (n > max_produce) n = max_produce;
    memcpy(received + received_size, src, n); received_size += n; *actual = n; return n ? 0 : 1;
}
static int32_t consume(uint64_t ctx, uint32_t id, void *dst, uint32_t capacity, uint32_t *actual) {
    assert(ctx == 9 && !lower_retained && qslot(id) == 1 && queue_live[1] && capacity == 256);
    if (tx_consumed) { *actual = 0; return RISC_STREAM_AGAIN; }
    memcpy(dst, "12345", 5); *actual = 5; tx_consumed = true; return 0;
}
static int32_t finish(uint64_t ctx, uint32_t id, int32_t why) {
    assert(ctx == 9 && (!lower_retained || why == RISC_STREAM_RETAINED));
    ++finishes;
    if (finishes == refuse_finish) return RISC_STREAM_BUSY;
    terminal[qslot(id)] = why; return 0;
}
static int32_t close_queue(uint64_t ctx, uint32_t id) {
    assert(ctx == 9 && !physical[0]); /* physical-first invariant */
    int i = qslot(id); assert(queue_live[i]); ++closes;
    if (closes == fail_close) return RISC_STREAM_BUSY;
    queue_live[i] = false; return 0;
}
static risc_usb_host_deadline_api_v1 timed = {1, sizeof(timed), now_ms, snapshot, configuration_timed, claim_timed, release_timed};
static risc_usb_host_deadlines_v1 host;
static const risc_stream_provider_v1 queues = {1, sizeof(queues), 9, publish, produce, consume, 0, 0, finish, close_queue};
static risc_serial_stream_open_v1 request = {1, sizeof(request), 2, 2, {115200,8,0,1,0}};
static risc_provider_stream_session_v1 opened;
static const risc_stream_session_provider_v1 *adapter;
static const risc_driver_v2 *driver;
static const risc_driver_poll_v2 *poll_driver;
static const risc_usb_cdc_api_v1 *serial;
static const risc_serial_port_streams_v1 *serial_streams;
static int32_t open_session(uint32_t ms) { opened.struct_size = sizeof(opened); return adapter->open(&request, sizeof(request), ms, &opened); }
static void clean_failure(int32_t wanted) {
    assert(open_session(250) == wanted);
    assert(!opened.session && !opened.rx_endpoint && !opened.tx_endpoint);
    assert(!physical[0] && !queue_live[0] && !queue_live[1]);
    assert(driver->quiesce());
}
static void assert_retained(void) {
    assert(!driver->quiesce());
    int before = releases + closes + reads + writes + controls + claims + configs + snapshots + finishes;
    driver->stop(); poll_driver->poll(250);
    assert(releases + closes + reads + writes + controls + claims + configs + snapshots + finishes == before);
    if (opened.session) assert(adapter->close(opened.session, 250) == RISC_STREAM_RETAINED);
    assert(!driver->start(0, 0));
}
int main(int argc, char **argv) {
    assert(argc == 3);
    _Static_assert(offsetof(risc_driver_stream_sessions_v2, extension_tag) == sizeof(risc_driver_poll_v2), "poll prefix");
    _Static_assert(offsetof(risc_usb_host_deadlines_v1, extension_tag) == sizeof(risc_usb_host_snapshot_v1), "host prefix");
    void *lib = dlopen(argv[1], RTLD_NOW); if (!lib) { puts(dlerror()); return 1; }
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get"); assert(get);
    driver = get(2); assert(driver && driver->struct_size == sizeof(risc_driver_stream_sessions_v2));
    const risc_driver_stream_sessions_v2 *extended = (const risc_driver_stream_sessions_v2 *)driver;
    assert(extended->extension_tag == RISC_DRIVER_STREAM_SESSIONS_TAG_V1 && extended->extension_version == 1);
    adapter = extended->stream_sessions; poll_driver = &extended->poll;
    serial = (const risc_usb_cdc_api_v1 *)driver->capability;
    serial_streams = (const risc_serial_port_streams_v1 *)driver->capability;
    host.snapshot.discovery.host = (risc_usb_host_api_v1){1,sizeof(host),(void *)1,configuration,claim,release_legacy,control,bulk_read,bulk_write};
    host.snapshot.discovery.poll = poll; host.snapshot.discovery.devices = devices;
    host.snapshot.discovery.release_checked = release_checked; host.snapshot.discovery.control_claim = control;
    host.extension_tag = RISC_USB_HOST_DEADLINES_TAG_V1; host.extension_version = 1; host.deadlines = &timed;
    risc_provider_dependency_v1 dependency = {"usb.host",1,&host};
    assert(poll_driver->streams.bind_streams(&queues)); assert(driver->start(&dependency,1));
    const char *scenario = argv[2];
    if (!strcmp(scenario,"legacy-host")) {
        host.snapshot.discovery.host.struct_size = sizeof(risc_usb_host_snapshot_v1); clean_failure(RISC_STREAM_UNSUPPORTED);
    } else if (!strcmp(scenario,"bad-suffix")) {
        host.extension_tag = 0; clean_failure(RISC_STREAM_UNSUPPORTED); host.extension_tag = RISC_USB_HOST_DEADLINES_TAG_V1;
        host.extension_version = 2; clean_failure(RISC_STREAM_UNSUPPORTED); host.extension_version = 1;
        host.snapshot.discovery.host.struct_size = sizeof(host) - 1; clean_failure(RISC_STREAM_UNSUPPORTED);
        host.snapshot.discovery.host.struct_size = sizeof(host);
        host.snapshot.discovery.control_claim = 0; clean_failure(RISC_STREAM_UNSUPPORTED);
        host.snapshot.discovery.control_claim = control;
        timed.struct_size = sizeof(timed) - 1; clean_failure(RISC_STREAM_UNSUPPORTED); timed.struct_size = sizeof(timed);
        timed.api_version = 2; clean_failure(RISC_STREAM_UNSUPPORTED); timed.api_version = 1;
        timed.release = 0; clean_failure(RISC_STREAM_UNSUPPORTED); assert(!calls && !publishes);
    } else if (!strcmp(scenario,"invalid")) {
        request.device_generation = 3; clean_failure(RISC_STREAM_INVALID); request.device_generation = 2;
        request.config.flow_control = 1; clean_failure(RISC_STREAM_INVALID); request.config.flow_control = 0;
        assert(open_session(0) == RISC_STREAM_INVALID && open_session(1001) == RISC_STREAM_INVALID && !calls);
    } else if (!strcmp(scenario,"inventory")) {
        for (snapshot_mode = 1; snapshot_mode <= 4; ++snapshot_mode) clean_failure(RISC_STREAM_IO);
        snapshot_mode = 0; device = 3; clean_failure(RISC_STREAM_DISCONNECTED); assert(!claims);
    } else if (!strcmp(scenario,"configure-short")) {
        fail_control = OPEN_CONTROLS; clean_failure(RISC_STREAM_IO); assert(releases == 1 && !publishes);
    } else if (!strcmp(scenario,"claim-fail")) {
        fail_claim = 1; clean_failure(RISC_STREAM_IO); assert(releases == 0);
    } else if (!strcmp(scenario,"claim-retained") || !strcmp(scenario,"claim-malformed")) {
        fail_claim = 1; failed_claim_token = true; claim_retained = !strcmp(scenario,"claim-retained");
        assert(open_session(250) == RISC_STREAM_RETAINED && opened.session && !opened.rx_endpoint && !releases); assert_retained();
    } else if (!strcmp(scenario,"partial-open")) {
        fail_publish = 2; clean_failure(RISC_STREAM_IO); assert(releases == 1 && closes == 1);
    } else if (!strcmp(scenario,"partial-retained")) {
        fail_publish = 2; fail_close = 1;
        assert(open_session(250) == RISC_STREAM_RETAINED && opened.session && opened.rx_endpoint && !opened.tx_endpoint);
        assert(!physical[0] && queue_live[0]); assert_retained();
    } else if (!strcmp(scenario,"deadline-clean")) {
        charge = 125; clean_failure(RISC_STREAM_TIMEOUT); assert(!claims && calls == 2);
    } else if (!strcmp(scenario,"deadline-retained")) {
        charge = 125 / 2 + 1; assert(open_session(250) == RISC_STREAM_RETAINED && opened.session && !publishes && !releases); assert_retained();
    } else if (!strcmp(scenario,"overrun")) {
        over_time = true; assert(open_session(250) == RISC_STREAM_RETAINED && !opened.session && !claims); assert_retained();
    } else if (!strcmp(scenario,"copied-open")) {
        mutate_request = &request; mutate_size = sizeof(request);
        assert(open_session(250) == 0 && !request.provider_device);
        assert(adapter->close(opened.session,250) == 0);
    } else if (!strcmp(scenario,"copied-call")) {
        assert(open_session(250) == 0);
        risc_serial_stream_call_v1 req = {1,sizeof(req),RISC_SERIAL_STREAM_CONFIGURE,0,{{115200,8,0,1,0}}};
        mutate_request = &req; mutate_size = sizeof(req); uint32_t actual = 99;
        assert(adapter->call(opened.session,&req,sizeof(req),250,0,0,&actual) == 0 && !actual && !req.api_version);
        assert(adapter->close(opened.session,250) == 0);
    } else if (!strcmp(scenario,"control-call-retained")) {
        assert(open_session(250)==0);
        risc_serial_stream_call_v1 req = {1,sizeof(req),RISC_SERIAL_STREAM_CONTROL_LINES,0,{{0}}};
        req.value.lines.dtr=req.value.lines.rts=1; uint32_t actual=99;
        fail_control=controls+1; control_result=RISC_STREAM_RETAINED;
        assert(adapter->call(opened.session,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_RETAINED && !actual);
        assert(finishes==2 && terminal[0]==RISC_STREAM_RETAINED && terminal[1]==RISC_STREAM_RETAINED && !releases && !closes); assert_retained();
    } else if (!strcmp(scenario,"control-retained")) {
        fail_control = OPEN_CONTROLS; control_result = RISC_STREAM_RETAINED;
        assert(open_session(250) == RISC_STREAM_RETAINED && opened.session && physical[0] && !releases && !publishes);
        assert(!finishes); assert_retained();
    } else if (!strcmp(scenario,"publish-malformed") || !strcmp(scenario,"publish-duplicate")) {
        malformed_publish = !strcmp(scenario,"publish-malformed"); fail_publish = malformed_publish ? 1 : 0;
        duplicate_publish = !malformed_publish;
        assert(open_session(250) == RISC_STREAM_RETAINED && opened.session && physical[0] && !releases);
        assert_retained();
    } else if (!strcmp(scenario,"cleanup-control") || !strcmp(scenario,"cleanup-unknown")) {
        assert(VENDOR != 1 && open_session(250) == 0);
        if (!strcmp(scenario,"cleanup-control")) fail_control = controls + 1;
        else snapshot_mode = 1;
        assert(adapter->close(opened.session,250) == RISC_STREAM_RETAINED && physical[0] && !releases && !closes);
        assert_retained();
    } else if (!strcmp(scenario,"io-retained") || !strcmp(scenario,"io-overrun") || !strcmp(scenario,"write-retained") || !strcmp(scenario,"finish-refused")) {
        assert(open_session(250) == 0); charge = 0;
        refuse_finish = !strcmp(scenario,"finish-refused") ? 1 : 0;
        retain_write = !strcmp(scenario,"write-retained");
        bulk_overrun = !strcmp(scenario,"io-overrun"); bulk_result = bulk_overrun ? 0 : RISC_STREAM_RETAINED;
        poll_driver->poll(250);
        assert(finishes==2 && received_size==(retain_write ? 2u : 0u) && !sent_size);
        assert(terminal[0]==(refuse_finish ? 0 : RISC_STREAM_RETAINED) && terminal[1]==RISC_STREAM_RETAINED);
        assert_retained();
    } else if (!strncmp(scenario,"short-",6)) {
        fail_control = atoi(scenario + 6); assert(fail_control > 0 && fail_control <= OPEN_CONTROLS);
        clean_failure(RISC_STREAM_IO); assert(!publishes && releases == 1);
    } else if (!strcmp(scenario,"descriptor-short")) {
        malformed_descriptor = true; clean_failure(RISC_STREAM_UNSUPPORTED); assert(!claims);
    } else if (!strcmp(scenario,"framing")) {
        request.config.data_bits = wanted_bits = 7; request.config.parity = wanted_parity = 2;
        request.config.stop_bits = wanted_stops = 2;
        assert(open_session(250) == 0 && adapter->close(opened.session,250) == 0);
    } else if (!strcmp(scenario,"old-chip")) {
        assert(VENDOR == 1); device_version = 0x27;
        request.config.data_bits = 7; clean_failure(RISC_STREAM_UNSUPPORTED); assert(!publishes);
        request.config.data_bits = 8; assert(open_session(250) == 0 && adapter->close(opened.session,250) == 0);
    } else if (!strcmp(scenario,"highspeed") || !strcmp(scenario,"ftx")) {
        assert(VENDOR == 3);
        device_pid = !strcmp(scenario,"highspeed") ? 0x6014 : 0x6015;
        device_bcd = !strcmp(scenario,"highspeed") ? 0x0900 : 0x1000;
        if (device_bcd == 0x0900) request.config.baud_rate = 12000000;
        assert(open_session(250) == 0 && adapter->close(opened.session,250) == 0);
    } else if (!strncmp(scenario,"status-",7)) {
        assert(VENDOR == 3); rx_mode = (unsigned)atoi(scenario+7);
        if (rx_mode == 3) { descriptor[22] = 0; descriptor[23] = 2; max_produce = 1024; }
        assert(open_session(250) == 0); charge = 0;
        poll_driver->poll(250);
        if (rx_mode == 1) assert(!received_size && !terminal[0]);
        else if (rx_mode == 2) assert(!received_size && terminal[0] == RISC_STREAM_IO);
        else {
            assert(received_size == 256 && reads == 1); snapshot_mode = 1;
            poll_driver->poll(250); assert(received_size == 256 && reads == 1);
            snapshot_mode = 0; poll_driver->poll(250); assert(received_size == 510 && reads == 1);
            for (size_t i = 0; i < 510; ++i) assert(received[i] == (uint8_t)i);
        }
        assert(adapter->close(opened.session,250) == 0);
    } else {
        assert(open_session(250) == 0 && opened.session && opened.rx_endpoint != opened.tx_endpoint);
        assert(calls == 3 + OPEN_CONTROLS && !memcmp(events,OPEN_EVENTS,3 + OPEN_CONTROLS));
        for (size_t i=0;i<3 + OPEN_CONTROLS;++i) assert(seen_ms[i] == 250-10*i);
#if VENDOR == 1
        const uint8_t order[] = {0x5f,0xa1,0x9a,0x9a};
        assert(values[2] == 0x1312 && values[3] == 0x2518);
#elif VENDOR == 2
        const uint8_t order[] = {0,0x1e,3};
#else
        const uint8_t order[] = {6,0,0,0,2,4,3};
        assert(values[1] == 0 && values[2] == 1 && values[3] == 2);
#endif
        assert(!memcmp(requests,order,sizeof(order)));
        uint64_t token = opened.session;
        assert(!serial->configure(token,115200,8,0,1));
        assert(!serial->control_lines(token,true,true));
        assert(!serial->close(token));
        { uint32_t rx=99,tx=99; assert(!serial_streams->endpoints(token,&rx,&tx) && !rx && !tx); }
        uint8_t byte=0; assert(serial->read(token,&byte,1,1)<0);
        assert(serial->write(token,&byte,1,1)<0);
        assert(!serial->open(2));
        risc_serial_stream_call_v1 req = {1,sizeof(req),RISC_SERIAL_STREAM_CONTROL_LINES,0,{{0}}};
        req.value.lines.dtr=req.value.lines.rts=1; uint32_t actual=99;
        assert(adapter->call(token+99,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_CLOSED && !actual);
        assert(adapter->call(token,&req,sizeof(req),250,0,0,&actual)==0 && !actual);
        assert(seen_ms[calls-1]==240);
        if (!strcmp(scenario,"io-presence")) {
            charge=0; error_read=true; poll_driver->poll(250);
            assert(terminal[0]==RISC_STREAM_IO && terminal[1]==RISC_STREAM_IO);
            req.operation=RISC_SERIAL_STREAM_CHECK_DEVICE; memset(&req.value,0,sizeof(req.value));
            assert(adapter->call(token,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_OK);
            assert(adapter->close(token,250)==0);
        } else if (!strcmp(scenario,"call-deadline")) {
            charge=250; int before=controls;
            assert(adapter->call(token,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_TIMEOUT && controls==before);
            charge=0; assert(adapter->close(token,250)==0);
        } else if (!strcmp(scenario,"call-overrun")) {
            over_time=true;
            assert(adapter->call(token,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_RETAINED);
            assert_retained();
        } else if (!strcmp(scenario,"close-deadline")) {
            charge=250; assert(adapter->close(token,250)==RISC_STREAM_RETAINED);
            assert(physical[0] == (VENDOR != 1) && queue_live[0] && queue_live[1]); assert_retained();
        } else if (!strcmp(scenario,"close-retained")) {
            fail_release=releases+1;
            assert(adapter->close(token,250)==RISC_STREAM_RETAINED && physical[0] && !closes);
            assert_retained();
        } else if (!strcmp(scenario,"queue-retained")) {
            fail_close=closes+2;
            assert(adapter->close(token,250)==RISC_STREAM_RETAINED && !queue_live[0] && queue_live[1]); assert_retained();
        } else {
            charge=250; poll_driver->poll(250); assert(!reads && !writes);
            charge=0; snapshot_mode=1; forbid_io=true; poll_driver->poll(250); assert(!reads && !writes);
            snapshot_mode=0; forbid_io=false;
            poll_driver->poll(250); assert(received_size==2 && sent_size==2);
            snapshot_mode=1; forbid_io=true; poll_driver->poll(250);
            assert(received_size==2 && sent_size==2);
            snapshot_mode=0; forbid_io=false;
            for(int i=0;i<4;++i) poll_driver->poll(250);
            assert(received_size==5 && !memcmp(received,"abcde",5) && sent_size==5 && !memcmp(sent,"12345",5));
            if (!strcmp(scenario,"disconnect")) {
                device=3; forbid_io=true; poll_driver->poll(250);
                assert(terminal[0]==RISC_STREAM_DISCONNECTED && terminal[1]==RISC_STREAM_DISCONNECTED);
                req.operation=RISC_SERIAL_STREAM_CHECK_DEVICE; memset(&req.value,0,sizeof(req.value));
                assert(adapter->call(token,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_DISCONNECTED);
            } else assert(!strcmp(scenario,"normal"));
            assert(adapter->close(token,250)==0 && driver->quiesce());
            assert(adapter->close(token,250)==RISC_STREAM_CLOSED);
            assert(adapter->call(token,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_CLOSED);
            if (!strcmp(scenario,"normal")) {
                assert(open_session(250)==0 && opened.session!=token);
                assert(adapter->close(opened.session,250)==0);
            }
        }
    }
    if (driver->quiesce()) { driver->stop(); assert(dlclose(lib)==0); }
    printf("%s tagged sessions: %s PASS\n",VENDOR_NAME,scenario); return 0;
}
