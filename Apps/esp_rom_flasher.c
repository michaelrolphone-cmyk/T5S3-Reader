#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_IMAGES 64
#define PATH_CAP 512
#define STATUS_CAP 128
#define FLASH_BLOCK 1024u
#define RX_CAP 2048u

#define ESP_SYNC       0x08u
#define ESP_FLASH_BEGIN 0x02u
#define ESP_FLASH_DATA  0x03u
#define ESP_FLASH_END   0x04u
#define ESP_SPI_ATTACH  0x0du
#define ESP_SPI_SET_PARAMS 0x0bu
#define ESP_FLASH_MD5   0x13u

static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_ui_api_v1 *ui;
static const t5_usb_api_v1 *usb;

static char images[MAX_IMAGES][T5_APP_DIRENT_NAME_MAX];
static uint32_t image_count;
static int32_t selected;
static char status_text[STATUS_CAP];
static uint8_t rxbuf[RX_CAP];

static void le32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static bool ends_with_bin(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n < 4) return false;
    const char *p = s + n - 4;
    return p[0]=='.' && (p[1]=='b'||p[1]=='B') && (p[2]=='i'||p[2]=='I') && (p[3]=='n'||p[3]=='N');
}

static void render_list(void) {
    t5_ui_list_row_t rows[MAX_IMAGES];
    for (uint32_t i=0;i<image_count;i++) {
        rows[i].title = images[i]; rows[i].subtitle = NULL; rows[i].value = NULL; rows[i].flags = 0;
    }
    t5_ui_chrome_t chrome = {
        .title="ESP ROM Flasher", .subtitle="Select merged firmware image from /sd",
        .status=image_count ? "Confirm to flash selected image" : "No .bin files found in /sd",
        .back_label="Back", .confirm_label="Flash", .previous_label="Up", .next_label="Down"
    };
    ui->render_list(&chrome, rows, image_count, selected);
}

static void render_status(const char *subtitle, const char *value, const char *status) {
    t5_ui_chrome_t chrome = {
        .title="ESP ROM Flasher", .subtitle=subtitle, .status=status,
        .back_label="Back", .confirm_label="OK", .previous_label="", .next_label=""
    };
    t5_ui_list_row_t row = { .title=image_count ? images[selected] : "", .subtitle=NULL, .value=value, .flags=T5_UI_LIST_HIGHLIGHT_VALUE };
    ui->render_list(&chrome, &row, 1, 0);
}

static void drain_rx(void) {
    uint8_t tmp[128];
    while (usb->serial_read(tmp, sizeof(tmp)) != 0) {}
}

static bool write_all(const uint8_t *data, size_t len, uint32_t timeout_ms) {
    size_t sent = 0;
    uint32_t start = app->millis();
    while (sent < len && (uint32_t)(app->millis() - start) < timeout_ms) {
        size_t n = usb->serial_write(data + sent, len - sent);
        if (n) sent += n;
        t5_app_input_t in; if (!app->poll(&in, 5)) return false;
    }
    return sent == len;
}

static size_t slip_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
    size_t o=0;
    if (o<cap) out[o++]=0xc0;
    for (size_t i=0;i<len;i++) {
        if (in[i]==0xc0) { if (o+2>cap) return 0; out[o++]=0xdb; out[o++]=0xdc; }
        else if (in[i]==0xdb) { if (o+2>cap) return 0; out[o++]=0xdb; out[o++]=0xdd; }
        else { if (o+1>cap) return 0; out[o++]=in[i]; }
    }
    if (o<cap) out[o++]=0xc0; else return 0;
    return o;
}

static bool recv_slip(uint8_t *out, size_t cap, size_t *out_len, uint32_t timeout_ms) {
    bool started=false, esc=false; size_t n=0; uint32_t start=app->millis();
    while ((uint32_t)(app->millis()-start) < timeout_ms) {
        uint8_t b[64]; size_t got=usb->serial_read(b,sizeof(b));
        for (size_t i=0;i<got;i++) {
            uint8_t c=b[i];
            if (!started) { if (c==0xc0) { started=true; n=0; esc=false; } continue; }
            if (c==0xc0) { if (n) { *out_len=n; return true; } continue; }
            if (esc) { c = c==0xdc ? 0xc0 : c==0xdd ? 0xdb : c; esc=false; }
            else if (c==0xdb) { esc=true; continue; }
            if (n>=cap) return false; out[n++]=c;
        }
        t5_app_input_t in; if (!app->poll(&in, 5)) return false;
    }
    return false;
}

static bool command(uint8_t op, const uint8_t *payload, uint16_t payload_len, uint32_t checksum, uint8_t *reply, size_t reply_cap, size_t *reply_len, uint32_t timeout_ms) {
    uint8_t raw[16 + FLASH_BLOCK];
    if ((size_t)payload_len + 8 > sizeof(raw)) return false;
    raw[0]=0x00; raw[1]=op; raw[2]=(uint8_t)payload_len; raw[3]=(uint8_t)(payload_len>>8); le32(raw+4,checksum);
    if (payload_len) memcpy(raw+8,payload,payload_len);
    uint8_t slip[2*(16+FLASH_BLOCK)+2];
    size_t slen=slip_encode(raw,payload_len+8,slip,sizeof(slip));
    if (!slen || !write_all(slip,slen,timeout_ms)) return false;
    if (!recv_slip(reply,reply_cap,reply_len,timeout_ms)) return false;
    return *reply_len >= 8 && reply[0]==0x01 && reply[1]==op && reply[*reply_len-2]==0x00;
}

static bool enter_bootloader(void) {
    drain_rx();
    usb->serial_set_control_lines(false, true); app->poll(&(t5_app_input_t){0}, 100);
    usb->serial_set_control_lines(true, true);  app->poll(&(t5_app_input_t){0}, 100);
    usb->serial_set_control_lines(false, false); app->poll(&(t5_app_input_t){0}, 50);
    return true;
}

static bool sync_rom(void) {
    uint8_t payload[36]; memset(payload,0x55,sizeof(payload));
    payload[0]=0x07; payload[1]=0x07; payload[2]=0x12; payload[3]=0x20;
    uint8_t reply[128]; size_t rlen=0;
    for (int i=0;i<7;i++) {
        if (command(ESP_SYNC,payload,sizeof(payload),0,reply,sizeof(reply),&rlen,500)) return true;
    }
    return false;
}

static uint8_t checksum(const uint8_t *data, size_t len) {
    uint8_t c=0xef; for (size_t i=0;i<len;i++) c^=data[i]; return c;
}

static bool flash_stream(const char *path, size_t size) {
    t5_storage_stream_t f = storage->stream_open(path, &size);
    if (!f) return false;
    uint8_t reply[160]; size_t rlen=0;
    uint32_t blocks=(uint32_t)((size + FLASH_BLOCK - 1u)/FLASH_BLOCK);
    uint8_t begin[16]; le32(begin,(uint32_t)size); le32(begin+4,blocks); le32(begin+8,FLASH_BLOCK); le32(begin+12,0);
    if (!command(ESP_FLASH_BEGIN,begin,sizeof(begin),0,reply,sizeof(reply),&rlen,10000)) { storage->stream_close(f); return false; }
    uint8_t block[FLASH_BLOCK]; uint8_t payload[16+FLASH_BLOCK];
    for (uint32_t seq=0; seq<blocks; ++seq) {
        size_t n=storage->stream_read(f,block,sizeof(block));
        if (!n && seq+1u<blocks) { storage->stream_close(f); return false; }
        if (n<sizeof(block)) memset(block+n,0xff,sizeof(block)-n);
        le32(payload,FLASH_BLOCK); le32(payload+4,seq); le32(payload+8,0); le32(payload+12,0); memcpy(payload+16,block,FLASH_BLOCK);
        if (!command(ESP_FLASH_DATA,payload,sizeof(payload),checksum(block,FLASH_BLOCK),reply,sizeof(reply),&rlen,5000)) { storage->stream_close(f); return false; }
        unsigned pct=(unsigned)(((uint64_t)(seq+1u)*100u)/blocks);
        snprintf(status_text,sizeof(status_text),"%u%% | block %lu/%lu",pct,(unsigned long)(seq+1u),(unsigned long)blocks);
        render_status("Flashing firmware - do not disconnect","Writing",status_text);
    }
    storage->stream_close(f);
    return true;
}

static bool verify_md5(size_t size) {
    uint8_t payload[16]; le32(payload,0); le32(payload+4,(uint32_t)size); le32(payload+8,0); le32(payload+12,0);
    uint8_t reply[160]; size_t rlen=0;
    if (!command(ESP_FLASH_MD5,payload,sizeof(payload),0,reply,sizeof(reply),&rlen,10000)) return false;
    return rlen >= 8 + 16 + 2;
}

static void reset_target(void) {
    uint8_t payload[4]; le32(payload,0);
    uint8_t reply[64]; size_t rlen=0; (void)command(ESP_FLASH_END,payload,sizeof(payload),0,reply,sizeof(reply),&rlen,500);
    usb->serial_set_control_lines(false,true); app->poll(&(t5_app_input_t){0},100);
    usb->serial_set_control_lines(false,false);
}

static bool wait_usb_ready(void) {
    t5_usb_serial_state_t st;
    uint32_t start=app->millis();
    while ((uint32_t)(app->millis()-start) < 8000u) {
        if (usb->serial_read_state(&st) && st.status==T5_USB_STATUS_READY) return true;
        t5_app_input_t in; if (!app->poll(&in,50)) return false;
    }
    return false;
}

static bool flash_selected(void) {
    char path[PATH_CAP]; snprintf(path,sizeof(path),"/sd/%s",images[selected]);
    size_t size=0;
    t5_storage_stream_t probe=storage->stream_open(path,&size);
    if (!probe || size==0 || size>0x1000000u) { if (probe) storage->stream_close(probe); return false; }
    storage->stream_close(probe);

    t5_usb_line_coding_t coding={115200,8,T5_USB_PARITY_NONE,1,0};
    render_status("Connecting to target","USB","Powering target and opening serial bridge");
    if (!usb->serial_start(&coding) || !wait_usb_ready()) return false;
    enter_bootloader();
    render_status("Connecting to target","ROM bootloader","Synchronizing");
    if (!sync_rom()) { usb->serial_stop(); return false; }
    render_status("ROM bootloader connected","Ready","Starting flash");
    if (!flash_stream(path,size)) { usb->serial_stop(); return false; }
    render_status("Verifying flash","MD5","Checking target flash contents");
    if (!verify_md5(size)) { usb->serial_stop(); return false; }
    render_status("Flash complete","Verified","Resetting target");
    reset_target();
    usb->serial_stop();
    return true;
}

__attribute__((visibility("default"))) void app_main(void) {
    app=t5_app_get_api(T5_APP_ABI_VERSION); storage=t5_storage_get_api(T5_STORAGE_API_VERSION);
    ui=t5_ui_get_api(T5_UI_API_VERSION); usb=t5_usb_get_api(T5_USB_API_VERSION);
    if (!app||!storage||!ui||!usb||!app->dir_open||!app->dir_next||!app->dir_close||!ui->render_list||!ui->poll_event||
        !storage->stream_open||!storage->stream_read||!storage->stream_close||!usb->serial_start||!usb->serial_set_control_lines||
        !usb->serial_read||!usb->serial_write||!usb->serial_read_state) return;
    image_count=0; selected=0;
    if (app->dir_open("/sd")) {
        t5_app_dirent_t e;
        while (image_count<MAX_IMAGES && app->dir_next(&e)) {
            if (!e.is_directory && ends_with_bin(e.name)) { strncpy(images[image_count],e.name,sizeof(images[image_count])-1); images[image_count][sizeof(images[image_count])-1]=0; image_count++; }
        }
        app->dir_close();
    }
    render_list();
    for (;;) {
        t5_ui_event_t ev; if (!ui->poll_event(&ev,50)) continue;
        if (ev.type==T5_UI_EVENT_EXIT || ev.type==T5_UI_EVENT_BACK) return;
        if (!image_count) continue;
        if (ev.type==T5_UI_EVENT_NEXT) { selected=ui->next_index(selected,image_count); render_list(); }
        else if (ev.type==T5_UI_EVENT_PREVIOUS) { selected=ui->previous_index(selected,image_count); render_list(); }
        else if (ev.type==T5_UI_EVENT_TAP) { int32_t h=ui->hit_test(ev.touch_x,ev.touch_y); if (h>=0 && h<(int32_t)image_count) { selected=h; render_list(); } }
        else if (ev.type==T5_UI_EVENT_CONFIRM) {
            bool ok=flash_selected();
            render_status(ok?"Flash complete":"Flash failed",ok?"Verified":"Error",ok?"Target reset into flashed firmware":"Check target connection/image and retry");
            for (;;) { t5_ui_event_t done; if (!ui->poll_event(&done,50)) continue; if (done.type==T5_UI_EVENT_BACK||done.type==T5_UI_EVENT_EXIT) return; if (done.type==T5_UI_EVENT_CONFIRM||done.type==T5_UI_EVENT_TAP) { render_list(); break; } }
        }
    }
}
