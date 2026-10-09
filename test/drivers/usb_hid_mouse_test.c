#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Drivers/usb_hid_mouse/driver.c"

/* Real production driver and parser; only dependencies are simulated. */
static const uint8_t descriptor[] = {
    0x05,1, 0x09,2, 0xa1,1, 0x09,1, 0xa1,0,
    0x05,9, 0x19,1, 0x29,3, 0x15,0, 0x25,1, 0x75,1, 0x95,3, 0x81,2,
    0x75,5, 0x95,1, 0x81,3,
    0x05,1, 0x09,0x30, 0x09,0x31, 0x09,0x38,
    0x15,0x81, 0x25,0x7f, 0x75,8, 0x95,3, 0x81,6,
    0x05,0x0c, 0x0a,0x38,2, 0x95,1, 0x81,6,
    0xc0, 0xc0
};
typedef struct {
    risc_usb_hid_interface_v1 item;
    const uint8_t *descriptor;
    size_t length;
    bool present, boot;
    uint64_t raw;
    uint8_t report[64];
    int32_t report_length;
    unsigned remaining;
} fake_device;
static fake_device devices[5];
static uint64_t now_ms = 100, raw_serial;
static unsigned calls, closes, opens, reads, yields, descriptor_reads, protocol_calls;
static unsigned close_failures, open_failures;
static uint64_t last_closed;
static bool scan_fail, descriptor_fail, protocol_fail;
static unsigned read_delay;
static fake_device *raw_device(uint64_t raw) {
    for (size_t i = 0; i < 5; ++i) if (raw && devices[i].raw == raw) return &devices[i];
    assert(!"unknown raw session"); return 0;
}
static bool fake_scan(void *ctx, size_t budget) {
    (void)ctx; ++calls; assert(budget == 16); return !scan_fail;
}
static bool fake_interfaces(void *ctx, risc_usb_hid_interface_v1 *out, size_t *n) {
    (void)ctx; ++calls; assert(*n >= 5); *n = 0;
    for (size_t i = 0; i < 5; ++i) if (devices[i].present) out[(*n)++] = devices[i].item;
    return true;
}
static uint64_t fake_open(void *ctx, uint64_t dev, uint8_t iface, uint8_t alt) {
    (void)ctx; ++calls; ++opens;
    if (open_failures) { --open_failures; return 0; }
    for (size_t i = 0; i < 5; ++i) if (devices[i].present &&
        same_interface(&devices[i].item, dev, iface, alt)) {
        assert(!devices[i].raw); return devices[i].raw = ++raw_serial;
    }
    assert(0); return 0;
}
static bool fake_descriptor(void *ctx, uint64_t raw, uint8_t *out, size_t *n) {
    (void)ctx; ++calls; ++descriptor_reads;
    fake_device *d = raw_device(raw);
    if (descriptor_fail) return false;
    assert(*n >= d->length); memcpy(out, d->descriptor, d->length); *n = d->length;
    return true;
}
static bool fake_boot(void *ctx, uint64_t raw, bool boot) {
    (void)ctx; ++calls; ++protocol_calls; raw_device(raw)->boot = boot;
    return !protocol_fail;
}
static int32_t fake_read(void *ctx, uint64_t raw, uint8_t *out, size_t n, uint32_t timeout) {
    (void)ctx; ++calls; ++reads; now_ms += read_delay;
    assert(n == 64 && timeout == 1);
    fake_device *d = raw_device(raw); assert(d->present);
    if (!d->remaining) return 0;
    --d->remaining;
    if (d->report_length > 0 && d->report_length <= 64)
        memcpy(out, d->report, (size_t)d->report_length);
    return d->report_length;
}
static bool fake_present(void *ctx, uint64_t raw) { (void)ctx; ++calls; return raw_device(raw)->present; }
static bool fake_close(void *ctx, uint64_t raw) {
    (void)ctx; ++calls; ++closes; last_closed = raw;
    fake_device *d = raw_device(raw);
    if (close_failures) { --close_failures; return false; }
    d->raw = 0; return true;
}
static uint64_t fake_now(void *ctx) { (void)ctx; ++calls; return now_ms; }
static void fake_sleep(void *ctx, uint32_t ms) { (void)ctx; ++calls; ++yields; assert(ms == 1); now_ms += ms; }
static const risc_usb_hid_api_v1 fake_hid = {1, sizeof(fake_hid), 0,
    fake_scan, fake_interfaces, fake_open, fake_descriptor, fake_boot, fake_read, fake_present, fake_close};
static const risc_platform_clock_api_v1 fake_clock = {1, sizeof(fake_clock), 0, fake_now, fake_sleep};
static const risc_provider_dependency_v1 deps[] = {{"usb.hid",1,&fake_hid},{"platform.clock",1,&fake_clock}};
static void fixture(void) {
    assert(!hid); memset(devices, 0, sizeof(devices));
    close_failures = open_failures = read_delay = 0;
    scan_fail = descriptor_fail = protocol_fail = false;
    assert(start(deps, 2));
}
static void attach(size_t i, uint64_t device, uint8_t iface, bool boot) {
    devices[i] = (fake_device){0};
    devices[i].item = (risc_usb_hid_interface_v1){.device=device,
        .interface_number=iface, .subclass=boot ? 1 : 0, .protocol=boot ? 2 : 0,
        .max_packet=64};
    devices[i].present = true; devices[i].descriptor = descriptor;
    devices[i].length = sizeof(descriptor);
}
static uint64_t baseline(uint64_t filter) {
    uint64_t token = subscribe(0, filter); assert(token);
    risc_usb_mouse_event_v1 e;
    assert(next(0, token, &e) == -2 && e.kind == RISC_USB_MOUSE_GAP);
    assert(next(0, token, &e) == -2);
    risc_usb_mouse_state_v1 states[4]; size_t n = 4;
    assert(snapshot(0, token, states, &n)); return token;
}
static void report(size_t i, uint8_t buttons, int x, int y, int wheel, int pan, unsigned times) {
    fake_device *d = &devices[i];
    d->report[0] = buttons; d->report[1] = (uint8_t)x; d->report[2] = (uint8_t)y;
    d->report[3] = (uint8_t)wheel; d->report[4] = (uint8_t)pan;
    d->report_length = d->boot ? 3 : 5; d->remaining = times;
}
static void finish(void) {
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) assert(unsubscribe(0, subscribers[i].token));
    for (unsigned i = 0; i < 8 && !quiesce(); ++i) {}
    assert(quiesce()); stop(); assert(status(0) == RISC_USB_MOUSE_STOPPED);
}
static void descriptor_tests(void) {
    mouse_layout l;
    assert(parse_layout(&l, descriptor, sizeof(descriptor)) && l.field_count == 7);
    for (size_t n = 0; n < sizeof(descriptor); ++n) assert(!parse_layout(&l, descriptor, n));
    uint8_t bad[1024];
    memcpy(bad, descriptor, sizeof(descriptor)); bad[3] = 5; assert(!parse_layout(&l,bad,sizeof(descriptor)));
    memcpy(bad, descriptor, sizeof(descriptor)); bad[47] = 2; assert(!parse_layout(&l,bad,sizeof(descriptor))); /* absolute axes */
    memcpy(bad, descriptor, sizeof(descriptor)); bad[37] = 0x30; assert(!parse_layout(&l,bad,sizeof(descriptor))); /* duplicate X */
    memcpy(bad, descriptor, sizeof(descriptor)); bad[40] = 0; assert(!parse_layout(&l,bad,sizeof(descriptor))); /* min unset/invalid item */
    memset(bad, 0xfe, sizeof(bad)); assert(!parse_layout(&l,bad,4));
    assert(!parse_layout(&l,bad,513));
    const uint8_t pop[] = {0xb4}; assert(!parse_layout(&l,pop,sizeof(pop)));
    const uint8_t push[] = {0xa4,0xa4,0xa4,0xa4,0xa4}; assert(!parse_layout(&l,push,sizeof(push)));
    memcpy(bad,descriptor,sizeof(descriptor)); memcpy(bad+sizeof(descriptor),descriptor,sizeof(descriptor));
    assert(!parse_layout(&l,bad,2*sizeof(descriptor))); /* two mouse applications */
    /* Report-ID selected layout, and an unrelated keyboard layout before it. */
    const uint8_t auxiliary[] = {0x05,1,0x09,6,0xa1,1,0x85,7,0x75,8,0x95,2,0x81,1,0xc0};
    memcpy(bad,auxiliary,sizeof(auxiliary)); bad[sizeof(auxiliary)]=0x85;bad[sizeof(auxiliary)+1]=42;
    memcpy(bad+sizeof(auxiliary)+2,descriptor,sizeof(descriptor));
    assert(parse_layout(&l,bad,sizeof(auxiliary)+2+sizeof(descriptor)) && l.report_id==42 && l.report_count==2);
    /* Delimiters, array axes, mixed implicit/report IDs, dangling locals, long items. */
    const uint8_t delimiter[] = {0xa9,1}; assert(!parse_layout(&l,delimiter,sizeof(delimiter)));
    memcpy(bad,descriptor,sizeof(descriptor));bad[47]=4;assert(!parse_layout(&l,bad,sizeof(descriptor)));
    memcpy(bad,descriptor,sizeof(descriptor));bad[sizeof(descriptor)]=0x85;bad[sizeof(descriptor)+1]=1;
    assert(!parse_layout(&l,bad,sizeof(descriptor)+2));
    /* Signed bit extraction checks every supported width and byte crossing. */
    for (unsigned width=2;width<=16;++width) {
        uint8_t bytes[8]={0}; mouse_field f={3,(uint8_t)width,1,-32768,32767,0};
        const int values[]={-(1<<(width-1)), -1, 0, (1<<(width-1))-1};
        for(unsigned j=0;j<4;++j){ memset(bytes,0,sizeof(bytes)); uint32_t v=(uint32_t)values[j]&((1u<<width)-1u);
            for(unsigned bit=0;bit<width;++bit) if(v&(1u<<bit)) bytes[(bit+3)/8]|=(uint8_t)(1u<<((bit+3)%8));
            assert(extract_field(bytes,&f)==values[j]);
        }
    }
    /* Deterministic malformed-descriptor corpus through production parser. */
    uint32_t seed=42;
    for(unsigned trial=0;trial<10000;++trial) {
        size_t n=trial%513;
        for(size_t i=0;i<n;++i){seed=seed*1664525u+1013904223u;bad[i]=(uint8_t)(seed>>24);}
        (void)parse_layout(&l,bad,n);
    }
}
static void report_layout_tests(void) {
    /* Actual production parse AND report apply at every supported signed width. */
    for (unsigned width = 2; width <= 16; ++width) {
        int minimum = -(1 << (width - 1)), maximum = (1 << (width - 1)) - 1;
        uint8_t data[] = {0x05,1,0x09,2,0xa1,1,0x09,0x30,0x09,0x31,
            0x16,(uint8_t)minimum,(uint8_t)(minimum >> 8),
            0x26,(uint8_t)maximum,(uint8_t)(maximum >> 8),
            0x75,(uint8_t)width,0x95,2,0x81,6,0xc0};
        mouse m = {0};
        assert(parse_layout(&m.layout, data, sizeof(data)));
        uint8_t bytes[4] = {0};
        uint32_t x = 1u << (width - 1u), y = (1u << (width - 1u)) - 1u;
        for (unsigned bit = 0; bit < width; ++bit) {
            if (x & (1u << bit)) bytes[bit/8] |= (uint8_t)(1u << (bit%8));
            if (y & (1u << bit)) bytes[(bit+width)/8] |= (uint8_t)(1u << ((bit+width)%8));
        }
        uint64_t sub;
        fixture(); sub = baseline(0);
        m.state = (risc_usb_mouse_state_v1){99,1,0,0,0,1,0};
        assert(apply_report(&m, bytes, (2u*width+7u)/8u));
        risc_usb_mouse_event_v1 e;
        assert(next(0,sub,&e)==1 && e.x==minimum && e.y==maximum);
        finish();
    }
    /* An auxiliary ID is length-validated and ignored, while the selected ID
     * delivers mouse events. Unknown/truncated IDs fail without changing state. */
    const uint8_t auxiliary[] = {0x05,1,0x09,6,0xa1,1,0x85,7,0x75,8,0x95,2,0x81,1,0xc0};
    uint8_t data[128];memcpy(data,auxiliary,sizeof(auxiliary));
    data[sizeof(auxiliary)]=0x85;data[sizeof(auxiliary)+1]=42;
    memcpy(data+sizeof(auxiliary)+2,descriptor,sizeof(descriptor));
    mouse m={0};assert(parse_layout(&m.layout,data,sizeof(auxiliary)+2+sizeof(descriptor)));
    fixture();uint64_t sub=baseline(0);m.state=(risc_usb_mouse_state_v1){99,1,0,0,0,1,0};
    uint8_t selected[]={42,5,0x81,0x7f,1,0xff};
    assert(apply_report(&m,selected,sizeof(selected)));
    risc_usb_mouse_event_v1 e;assert(next(0,sub,&e)==1&&e.x==-127&&e.y==127&&e.wheel==1&&e.pan==-1&&e.pressed==5);
    uint8_t other[]={7,9,9};assert(apply_report(&m,other,sizeof(other))&&next(0,sub,&e)==0&&m.state.buttons==5);
    assert(!apply_report(&m,other,2));other[0]=9;assert(!apply_report(&m,other,3));
    assert(!apply_report(&m,selected,5));selected[2]=0x80;assert(!apply_report(&m,selected,6)&&m.state.buttons==5);
    finish();
}
static void normal_tests(void) {
    fixture(); uint64_t all=baseline(0), filtered=baseline(100);
    attach(0,100,2,false); report(0,3,-10,20,1,-2,1);
    assert(poll(0,16)); risc_usb_mouse_event_v1 e;
    assert(next(0,all,&e)==1 && e.kind==1 && e.state.device==100 && e.state.interface_number==2);
    uint64_t session=e.state.session;
    assert(next(0,all,&e)==1 && e.kind==3 && e.pressed==3 && !e.released && e.x==-10 && e.y==20 && e.wheel==1 && e.pan==-2);
    unsigned before=reads; assert(poll(0,16) && reads==before+1); /* quiet read preserves held state */
    report(0,2,0,0,0,0,1); assert(poll(0,1));
    assert(next(0,all,&e)==1 && e.released==1 && e.state.buttons==2);
    attach(1,100,3,false); attach(2,200,0,true);
    assert(poll(0,16));assert(poll(0,16));
    assert(!devices[2].boot); /* boot-capable descriptor keeps wheel/pan */
    risc_usb_mouse_state_v1 states[4];size_t n=1;
    assert(!snapshot(0,all,states,&n)&&n==3); n=4;
    assert(snapshot(0,filtered,states,&n)&&n==2 && states[0].session!=states[1].session);
    n=4;assert(snapshot(0,all,states,&n)&&n==3);
    devices[0].present=false; assert(poll(0,16));
    assert(next(0,all,&e)==1 && e.kind==2 && e.released==2 && !e.state.connected && !e.state.buttons && e.state.session==session);
    assert(poll(0,16)); assert(next(0,all,&e)==0); /* no duplicate disconnect */
    attach(0,101,2,false);assert(poll(0,16));assert(next(0,all,&e)==1 && e.state.session!=session);
    assert(!quiesce()); finish();
}
static void overflow_tests(void) {
    fixture(); attach(0,100,0,false);uint64_t slow=baseline(0),fast=baseline(0);
    assert(poll(0,1));risc_usb_mouse_state_v1 s[4];size_t n=4;assert(snapshot(0,slow,s,&n));n=4;assert(snapshot(0,fast,s,&n));
    risc_usb_mouse_event_v1 e;
    for(unsigned i=0;i<40;++i){report(0,(uint8_t)(i&1),1,0,0,0,1);assert(poll(0,1));assert(next(0,fast,&e)==1);}
    assert(next(0,slow,&e)==-2 && next(0,slow,&e)==-2);
    n=0;assert(!snapshot(0,slow,0,&n)&&n==1 && next(0,slow,&e)==-2);
    n=4;assert(snapshot(0,slow,s,&n)&&n==1&&s[0].buttons==1 && next(0,slow,&e)==0);
    report(0,0,0,0,0,0,1);assert(poll(0,1));assert(next(0,slow,&e)==1&&e.released==1);
    assert(unsubscribe(0,slow));assert(next(0,slow,&e)==-1); assert(baseline(0)!=slow);finish();
}
static void faults_tests(void) {
    fixture();attach(0,100,0,false);uint64_t sub=baseline(0);report(0,1,1,0,0,0,1);assert(poll(0,1));
    devices[0].report_length=-1;devices[0].remaining=1;assert(!poll(0,1));
    risc_usb_mouse_event_v1 e;assert(next(0,sub,&e)==-2);risc_usb_mouse_state_v1 s[4];size_t n=4;
    assert(snapshot(0,sub,s,&n)&&n==0&&!devices[0].raw);unsigned before=opens;
    assert(poll(0,1)&&opens==before); /* failed attachment does not endlessly reopen */
    finish();
    for(int malformed=1;malformed<=3;++malformed){
        fixture();attach(0,100,0,false);sub=baseline(0);assert(poll(0,1));report(0,1,1,0,0,0,1);
        devices[0].report_length=malformed==1?4:malformed==2?6:65;
        assert(!poll(0,1)&&next(0,sub,&e)==-2);n=4;assert(snapshot(0,sub,s,&n)&&n==0);finish();
    }
    fixture();attach(0,100,0,true);devices[0].length=0;sub=baseline(0);assert(poll(0,1)&&devices[0].boot);
    report(0,7,-128,127,0,0,1);assert(poll(0,1));assert(next(0,sub,&e)==1&&e.kind==1);
    assert(next(0,sub,&e)==1&&e.x==-128&&e.y==127&&e.pressed==7);finish();
    fixture();attach(0,100,0,false);sub=baseline(0);report(0,1,1,0,0,0,1);assert(poll(0,1));
    scan_fail=true;assert(!poll(0,1)&&next(0,sub,&e)==-2);n=4;assert(snapshot(0,sub,s,&n)&&n==0);scan_fail=false;finish();
}
static void scheduling_tests(void) {
    fixture();attach(0,100,0,false);open_failures=10;
    unsigned before=opens;assert(!poll(0,1)&&opens==before+1);assert(poll(0,1)&&opens==before+1);
    now_ms+=100;assert(!poll(0,1));now_ms+=100;assert(!poll(0,1));now_ms+=100;
    assert(poll(0,1)&&opens==before+3);finish();
    fixture();attach(0,100,0,false);assert(poll(0,1));report(0,1,1,0,0,0,100);
    before=reads;read_delay=10;assert(poll(0,16)&&reads==before+2);finish();
    fixture();for(unsigned i=0;i<5;++i)attach(i,100+i,0,false);
    for(unsigned i=0;i<5;++i)assert(poll(0,1));
    before=opens;assert(poll(0,16)&&opens==before);
    for(unsigned i=0;i<4;++i)report(i,1,1,0,0,0,1);
    before=reads;for(unsigned i=0;i<4;++i)assert(poll(0,1));assert(reads==before+4);
    for(unsigned i=0;i<4;++i)assert(devices[i].remaining==0);
    finish();
}
static void admission_tests(void) {
    assert(!start(0,2) && !start(deps,1));
    risc_usb_hid_api_v1 short_hid=fake_hid;short_hid.struct_size=8;
    risc_platform_clock_api_v1 no_sleep=fake_clock;no_sleep.sleep_ms=0;
    risc_provider_dependency_v1 invalid[]={deps[0],deps[1]};
    invalid[0].api=&short_hid;assert(!start(invalid,2));
    invalid[0]=deps[0];invalid[1].api=&no_sleep;assert(!start(invalid,2));
    fixture();uint64_t tokens[4];for(unsigned i=0;i<4;++i)tokens[i]=baseline(i);
    assert(!subscribe(0,0));assert(!poll(0,0)&&!poll(0,17));
    assert(unsubscribe(0,tokens[1])&&!unsubscribe(0,tokens[1]));assert(baseline(0)!=tokens[1]);finish();
    fixture();attach(0,100,0,true);protocol_fail=true;unsigned before=protocol_calls;
    assert(!poll(0,1)&&!devices[0].raw&&protocol_calls==before+1);
    now_ms+=100;assert(!poll(0,1));now_ms+=100;assert(!poll(0,1));now_ms+=100;
    assert(poll(0,1)&&protocol_calls==before+3);finish();
    fixture();attach(0,100,0,false);devices[0].length=0;before=descriptor_reads;
    assert(!poll(0,1)&&!devices[0].raw);for(unsigned i=0;i<5;++i)assert(poll(0,1));
    assert(descriptor_reads==before+1);finish();
}
static void close_tests(void) {
    fixture();for(unsigned i=0;i<4;++i)attach(i,100+i,0,false);
    uint64_t multi=baseline(0);for(unsigned i=0;i<4;++i)assert(poll(0,1));
    uint64_t retained_raw=devices[2].raw;devices[2].present=false;close_failures=2;
    assert(!poll(0,1)&&last_closed==retained_raw&&phase==RISC_USB_MOUSE_CLOSING);
    unsigned multi_before=calls;
    assert(!poll(0,1)&&calls==multi_before+1&&last_closed==retained_raw);
    multi_before=calls;
    assert(!poll(0,1)&&calls==multi_before+1&&last_closed==retained_raw&&!devices[2].raw);
    /* Only after custody succeeds may the other semantically disconnected
     * mice close, one per invocation. No device is read or reopened. */
    assert(devices[0].raw&&devices[1].raw&&devices[3].raw);
    for(unsigned i=0;i<3;++i){multi_before=calls;(void)poll(0,1);assert(calls==multi_before+1);}
    assert(phase==RISC_USB_MOUSE_RUNNING);risc_usb_mouse_event_v1 multi_event;
    assert(next(0,multi,&multi_event)==-2);size_t multi_n=0;assert(snapshot(0,multi,0,&multi_n)&&multi_n==0);
    finish();

    fixture();attach(0,100,0,false);uint64_t sub=baseline(0);assert(poll(0,1));
    close_failures=1;devices[0].present=false;assert(!poll(0,1)&&status(0)==RISC_USB_MOUSE_CLOSING);
    uint64_t raw=devices[0].raw;unsigned before=calls;
    assert(poll(0,1)&&calls==before+1&&!devices[0].raw&&raw);finish();
    fixture();attach(0,100,0,false);descriptor_fail=true;close_failures=1;sub=baseline(0);
    assert(!poll(0,1)&&devices[0].raw&&status(0)==RISC_USB_MOUSE_CLOSING);
    before=calls;assert(poll(0,1)&&calls==before+1);descriptor_fail=false;now_ms+=100;assert(poll(0,1));finish();
    fixture();attach(0,100,0,false);sub=baseline(0);report(0,1,1,1,1,1,1);assert(poll(0,1));
    close_failures=100;devices[0].present=false;
    assert(!poll(0,1)&&phase==RISC_USB_MOUSE_CLOSING);before=calls;
    assert(!poll(0,1)&&calls==before+1);before=calls;
    assert(!poll(0,1)&&calls==before+1&&phase==RISC_USB_MOUSE_RETAINED);
    before=calls;assert(!poll(0,1));assert(!quiesce());stop();assert(!start(deps,2));assert(!subscribe(0,0));
    risc_usb_mouse_event_v1 e;assert(next(0,sub,&e)==-2);size_t n=0;assert(snapshot(0,sub,0,&n)&&n==0);
    assert(unsubscribe(0,sub));assert(!quiesce());stop();assert(calls==before&&devices[0].raw);
}
int main(void) {
    assert(t5_driver_get(2)==&driver&&!t5_driver_get(1));
    descriptor_tests();report_layout_tests();normal_tests();overflow_tests();faults_tests();scheduling_tests();admission_tests();close_tests();
    printf("USB mouse production parser, copied events, recovery, bounds and terminal retention: PASS (%u cooperative yields)\n",yields);
    return 0;
}
