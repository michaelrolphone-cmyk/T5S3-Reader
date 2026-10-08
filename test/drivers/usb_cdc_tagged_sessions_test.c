#include "RiscUsbHostDeadlinesV1.h"
#include "RiscStreamSessionProviderV1.h"
#include "../../sdk/app/RiscSerialStreamSessionV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* Execute the actual separately compiled class ELF. Simulated host and queue
 * callbacks enforce ordering and exact physical/queue custody independently. */
static const uint8_t descriptor[] = {9,2,41,0,2,1,0,0x80,50,9,4,0,0,0,2,2,1,0,
    9,4,1,0,2,10,0,0,0,7,5,0x81,2,64,0,0,7,5,0x02,2,64,0,0};
static uint64_t tick, device = 2;
static uint32_t charge = 10, max_produce = 2, max_write = 2;
static int snapshots, configs, claims, controls, releases, publishes, closes, reads, writes;
static int fail_claim, fail_release, fail_publish, fail_close, snapshot_mode, control_short;
static bool over_time, failed_claim_token, claim_retained, physical[2], queue_live[2];
static bool rx_sent, tx_consumed, forbid_io, configured, error_read;
static bool retain_control, retain_read, retain_write, lower_retained;
static unsigned queue_produces, queue_consumes, queue_finishes;
static uint32_t seen_ms[64]; static char events[128]; static size_t calls;
static uint8_t received[64], sent[64]; static size_t received_size, sent_size;
static uint32_t queue_ids[2]; static int32_t terminal[2];
static void step(char event, uint32_t ms) {
    assert(!lower_retained && calls < sizeof(events) && ms && ms <= 1000);
    events[calls] = event; seen_ms[calls++] = ms;
    tick += over_time ? ms + 1 : charge;
}
static uint64_t now_ms(void *ctx) { assert(ctx == (void *)1 && !lower_retained); return tick; }
static int32_t snapshot(void *ctx, uint64_t *out, size_t *count, uint32_t ms) {
    assert(ctx == (void *)1 && *count == 8); ++snapshots; step('S', ms);
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
    *vid = 0x1234; *pid = 0x5678; return true;
}
static int32_t configuration_timed(void *ctx, uint64_t token, uint8_t *out, size_t *len,
                                   uint16_t *vid, uint16_t *pid, uint32_t ms) {
    ++configs; step('D', ms); return configuration(ctx, token, out, len, vid, pid) ? 0 : -5;
}
static bool claim(void *ctx, uint64_t token, uint8_t iface, uint8_t alt, uint64_t *out) {
    assert(ctx == (void *)1 && token == device && iface < 2 && !alt && !physical[iface]);
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
    assert(ctx == (void *)1 && token >= 100 && token <= 101 && physical[token - 100]);
    if (releases == fail_release) return false;
    physical[token - 100] = false; return true;
}
static void release_legacy(void *ctx, uint64_t token) { ++releases; (void)release_checked(ctx, token); }
static int32_t release_timed(void *ctx, uint64_t token, uint32_t ms) {
    ++releases; step(token == 101 ? 'b' : 'a', ms); return release_checked(ctx, token) ? 0 : -5;
}
static int32_t control(void *ctx, uint64_t token, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t iface, uint8_t *payload, uint16_t n, uint32_t ms) {
    assert(ctx == (void *)1 && token == 100 && physical[0] && physical[1] && type == 0x21 && !iface);
    ++controls; step('C', ms);
    if (request == 0x20) {
        assert(n == 7 && payload && payload[0] == 0 && payload[1] == 0xc2 && payload[2] == 1);
        assert(payload[4] == 0 && payload[5] == 0 && payload[6] == 8); configured = true;
    } else assert(request == 0x22 && value == 3 && !n && !payload);
    if (retain_control) { lower_retained=true; return RISC_STREAM_RETAINED; }
    return control_short ? (n ? n - 1 : -1) : n;
}
static int32_t bulk_read(void *ctx, uint64_t token, uint8_t ep, uint8_t *dst, size_t n, uint32_t ms) {
    assert(ctx == (void *)1 && token == 101 && ep == 0x81 && n == 256 && physical[1] && !forbid_io);
    ++reads; assert(ms == 1);
    if (retain_read) { lower_retained=true; return RISC_STREAM_RETAINED; }
    if (error_read) return -1;
    if (rx_sent) return 0;
    memcpy(dst, "abcde", 5); rx_sent = true; return 5;
}
static int32_t bulk_write(void *ctx, uint64_t token, uint8_t ep, const uint8_t *src, size_t n, uint32_t ms) {
    assert(ctx == (void *)1 && token == 101 && ep == 2 && physical[1] && !forbid_io);
    ++writes; assert(ms == 1);
    if (retain_write) { lower_retained=true; return RISC_STREAM_RETAINED; }
    if (n > max_write) n = max_write;
    memcpy(sent + sent_size, src, n); sent_size += n; return (int32_t)n;
}
static bool poll(void *ctx, size_t maximum, size_t *done) { (void)ctx; (void)maximum; *done = 0; return true; }
static bool devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx; assert(*count >= 1); out[0] = device; *count = device ? 1 : 0; return true;
}
static int32_t publish(uint64_t ctx, const risc_stream_endpoint_v1 *spec, uint32_t *out) {
    assert(ctx == 9 && configured && physical[0] && physical[1] && spec->kind == 1 && spec->byte_capacity == 2048);
    ++publishes; if (publishes == fail_publish) return RISC_STREAM_BUSY;
    unsigned slot = spec->rights == RISC_STREAM_READ ? 0 : 1;
    assert(spec->rights == (slot ? RISC_STREAM_WRITE : RISC_STREAM_READ) && !queue_live[slot]);
    queue_live[slot] = true; queue_ids[slot] = (uint32_t)(1000 + publishes); *out = queue_ids[slot]; return 0;
}
static int qslot(uint32_t id) { if (id == queue_ids[0]) return 0; assert(id == queue_ids[1]); return 1; }
static int32_t produce(uint64_t ctx, uint32_t id, const void *src, uint32_t n, uint32_t *actual) {
    assert(!lower_retained && ctx == 9 && qslot(id) == 0 && queue_live[0]); ++queue_produces;
    if (n > max_produce) n = max_produce;
    memcpy(received + received_size, src, n); received_size += n; *actual = n; return n ? 0 : 1;
}
static int32_t consume(uint64_t ctx, uint32_t id, void *dst, uint32_t capacity, uint32_t *actual) {
    assert(!lower_retained && ctx == 9 && qslot(id) == 1 && queue_live[1] && capacity == 256); ++queue_consumes;
    if (tx_consumed) { *actual = 0; return RISC_STREAM_AGAIN; }
    memcpy(dst, "12345", 5); *actual = 5; tx_consumed = true; return 0;
}
static int32_t finish(uint64_t ctx, uint32_t id, int32_t why) { assert(ctx == 9); ++queue_finishes; terminal[qslot(id)] = why; return 0; }
static int32_t close_queue(uint64_t ctx, uint32_t id) {
    assert(!lower_retained && ctx == 9 && !physical[0] && !physical[1]); /* physical-first invariant */
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
static const risc_serial_port_streams_v1 *serial;
static int32_t open_session(uint32_t ms) { opened.struct_size = sizeof(opened); return adapter->open(&request, sizeof(request), ms, &opened); }
static void clean_failure(int32_t wanted) {
    assert(open_session(250) == wanted);
    assert(!opened.session && !opened.rx_endpoint && !opened.tx_endpoint);
    assert(!physical[0] && !physical[1] && !queue_live[0] && !queue_live[1]);
    assert(driver->quiesce());
}
static void assert_retained(void) {
    assert(!driver->quiesce());
    int before = releases + closes + reads + writes;
    driver->stop(); poll_driver->poll(250);
    assert(releases + closes + reads + writes == before);
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
    serial = (const risc_serial_port_streams_v1 *)driver->capability;
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
        timed.release = 0; clean_failure(RISC_STREAM_UNSUPPORTED);
    } else if (!strcmp(scenario,"invalid")) {
        request.device_generation = 3; clean_failure(RISC_STREAM_INVALID); request.device_generation = 2;
        request.config.flow_control = 1; clean_failure(RISC_STREAM_INVALID); request.config.flow_control = 0;
        assert(open_session(0) == RISC_STREAM_INVALID && open_session(1001) == RISC_STREAM_INVALID && !calls);
    } else if (!strcmp(scenario,"inventory")) {
        for (snapshot_mode = 1; snapshot_mode <= 4; ++snapshot_mode) clean_failure(RISC_STREAM_IO);
        snapshot_mode = 0; device = 3; clean_failure(RISC_STREAM_DISCONNECTED); assert(!claims);
    } else if (!strcmp(scenario,"configure-short")) {
        control_short = 1; clean_failure(RISC_STREAM_IO); assert(releases == 2 && !publishes);
    } else if (!strcmp(scenario,"open-control-retained")) {
        retain_control=true;
        assert(open_session(250)==RISC_STREAM_RETAINED && opened.session && !opened.rx_endpoint && !opened.tx_endpoint);
        assert(physical[0] && physical[1] && !releases && !publishes && !queue_finishes); assert_retained();
    } else if (!strcmp(scenario,"claim-fail")) {
        fail_claim = 2; clean_failure(RISC_STREAM_IO); assert(releases == 1);
    } else if (!strcmp(scenario,"claim-retained") || !strcmp(scenario,"claim-malformed")) {
        fail_claim = 2; failed_claim_token = true; claim_retained = !strcmp(scenario,"claim-retained");
        assert(open_session(250) == RISC_STREAM_RETAINED && opened.session && !opened.rx_endpoint && !releases); assert_retained();
    } else if (!strcmp(scenario,"partial-open")) {
        fail_publish = 2; clean_failure(RISC_STREAM_IO); assert(releases == 2 && closes == 1);
    } else if (!strcmp(scenario,"partial-retained")) {
        fail_publish = 2; fail_close = 1;
        assert(open_session(250) == RISC_STREAM_RETAINED && opened.session && opened.rx_endpoint && !opened.tx_endpoint);
        assert(!physical[0] && !physical[1] && queue_live[0]); assert_retained();
    } else if (!strcmp(scenario,"deadline-clean")) {
        charge = 125; clean_failure(RISC_STREAM_TIMEOUT); assert(!claims && calls == 2);
    } else if (!strcmp(scenario,"deadline-retained")) {
        charge = 50; assert(open_session(250) == RISC_STREAM_RETAINED && opened.session && !publishes && !releases); assert_retained();
    } else if (!strcmp(scenario,"overrun")) {
        over_time = true; assert(open_session(250) == RISC_STREAM_RETAINED && !opened.session && !claims); assert_retained();
    } else {
        assert(open_session(250) == 0 && opened.session && opened.rx_endpoint != opened.tx_endpoint);
        assert(calls == 5 && !memcmp(events,"SDABC",5));
        for (size_t i=0;i<5;++i) assert(seen_ms[i] == 250-10*i);
        uint64_t token = opened.session;
        assert(!serial->inventory.discovery.serial.configure(token,115200,8,0,1));
        assert(!serial->inventory.discovery.serial.control_lines(token,true,true));
        assert(!serial->inventory.discovery.serial.close(token));
        uint32_t rx=99,tx=99; assert(!serial->endpoints(token,&rx,&tx) && !rx && !tx);
        uint8_t byte=0; assert(serial->inventory.discovery.serial.read(token,&byte,1,1)<0);
        assert(serial->inventory.discovery.serial.write(token,&byte,1,1)<0);
        assert(!serial->inventory.discovery.serial.open(2));
        risc_serial_stream_call_v1 req = {1,sizeof(req),RISC_SERIAL_STREAM_CONTROL_LINES,0,{{0}}};
        req.value.lines.dtr=req.value.lines.rts=1; uint32_t actual=99;
        assert(adapter->call(token+99,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_CLOSED && !actual);
        assert(adapter->call(token,&req,sizeof(req),250,0,0,&actual)==0 && !actual);
        assert(seen_ms[calls-1]==240);
        if (!strcmp(scenario,"configure-retained") || !strcmp(scenario,"lines-retained") ||
            !strcmp(scenario,"read-retained") || !strcmp(scenario,"write-retained")) {
            charge=0; poll_driver->poll(250);
            assert(received_size==2 && sent_size==2); // Both staging buffers are partial.
            if (!strcmp(scenario,"configure-retained") || !strcmp(scenario,"lines-retained")) {
                retain_control=true;
                if (!strcmp(scenario,"configure-retained")) {
                    req.operation=RISC_SERIAL_STREAM_CONFIGURE; req.value.config=request.config;
                }
                assert(adapter->call(token,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_RETAINED && !actual);
            } else if (!strcmp(scenario,"write-retained")) {
                retain_write=true; poll_driver->poll(250); assert(received_size==4 && sent_size==2);
            } else {
                retain_read=true; poll_driver->poll(250); poll_driver->poll(250);
                assert(received_size==5 && sent_size==4);
            }
            assert(lower_retained && physical[0] && physical[1] && !releases && !closes);
            assert(terminal[0]==RISC_STREAM_RETAINED && terminal[1]==RISC_STREAM_RETAINED && queue_finishes==2);
            unsigned produced=queue_produces,consumed=queue_consumes,finished=queue_finishes;
            assert_retained();
            assert(queue_produces==produced && queue_consumes==consumed && queue_finishes==finished);
        } else if (!strcmp(scenario,"io-presence")) {
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
            charge=125; assert(adapter->close(token,250)==RISC_STREAM_RETAINED);
            assert(!physical[0] && !physical[1] && queue_live[0] && queue_live[1]); assert_retained();
        } else if (!strcmp(scenario,"close-retained")) {
            fail_release=releases+2;
            assert(adapter->close(token,250)==RISC_STREAM_RETAINED && !physical[1] && physical[0] && !closes);
            assert_retained();
        } else if (!strcmp(scenario,"queue-retained")) {
            fail_close=closes+2;
            assert(adapter->close(token,250)==RISC_STREAM_RETAINED && !queue_live[0] && queue_live[1]); assert_retained();
        } else {
            charge=250; poll_driver->poll(250); assert(!reads && !writes);
            charge=0; snapshot_mode=1; forbid_io=true; poll_driver->poll(250); assert(!reads && !writes);
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
                uint64_t raw=serial->inventory.discovery.serial.open(2); assert(raw);
                assert(adapter->close(raw,250)==RISC_STREAM_CLOSED);
                assert(adapter->call(raw,&req,sizeof(req),250,0,0,&actual)==RISC_STREAM_CLOSED);
                assert(serial->inventory.discovery.serial.close(raw));
            }
        }
    }
    if (driver->quiesce()) { driver->stop(); assert(dlclose(lib)==0); }
    printf("CDC tagged sessions: %s PASS\n",scenario); return 0;
}
