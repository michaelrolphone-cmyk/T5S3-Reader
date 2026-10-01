/* Native ESP32-S3 camera backend, linked inside the installed ELF.
 * Register setup derived from Espressif esp32-camera v2.0.4 target/esp32s3
 * ll_cam.c (Apache-2.0). Sensor implementation is retained under vendor/.
 * Dedicated SCCB GPIO, no firmware I2C/camera/GPIO/LEDC driver imports.
 * No ISR/task/callback points into this module: owner polls latched events.
 * 96KiB internal DMA, finite noncircular descriptors, one SVGA JPEG.
 */
#include "hardware.h"
#include "hardware_diag.h"
#include "hardware_frame.h"
#include "T5StreamApi.h"
#include "vendor/ov3660.h"
#include "soc/soc.h"
#include "soc/system_reg.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/lcd_cam_struct.h"
#include "soc/gdma_struct.h"
#include "soc/lldesc.h"
#include "esp_rom_gpio.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <time.h>
#include <unistd.h>
/* Linker peripheral aliases must not be unresolved firmware imports. */
#define CAM (*(volatile lcd_cam_dev_t *)DR_REG_LCD_CAM_BASE)
#define DMA (*(volatile gdma_dev_t *)DR_REG_GDMA_BASE)
#define PADS (*(volatile gpio_dev_t *)DR_REG_GPIO_BASE)
#define NODES 96u
#define NODE_BYTES 1024u
#define FRAME_LIMIT (NODES*NODE_BYTES)
static risc_camera_esp32s3_profile_v1 pins;
static sensor_t sensor;
static lldesc_t *desc;
static uint8_t *buffer;
static bool touched, dma_owned, active, waiting, fault;
static uint32_t sampled_gpio, sampled_changes, available, harvested;
static cam_jpeg_scan jpeg;
static uint64_t init_deadline;
uint64_t cam_hw_now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t) || t.tv_sec<0)return UINT64_MAX;
    return (uint64_t)t.tv_sec*1000u+(unsigned)t.tv_nsec/1000000u;
}
void cam_hw_yield(void) { usleep(1000); }
static uint32_t cycles(void) { uint32_t v; __asm__ volatile("rsr.ccount %0":"=a"(v)); return v; }
/* At max 240MHz this is >=5us; lower CPU clocks only slow SCCB down.
 * CPU ccount is local, never a peripheral/timer firmware bridge. */
static void edge(void) { uint32_t b=cycles();while((uint32_t)(cycles()-b)<1200u){} }
static bool level(unsigned pin) { return (PADS.in>>pin)&1u; }
static void drive(unsigned pin,bool high) {
    if(high) PADS.enable_w1tc=1u<<pin; else PADS.enable_w1ts=1u<<pin;
    edge();
}
static bool high_clock(void) {
    drive(pins.scl,true);uint32_t b=cycles();
    while(!level(pins.scl)){if((uint32_t)(cycles()-b)>120000u)return false;}
    return true;
}
static bool start_sccb(void) {
    drive(pins.sda,true);if(!high_clock() || !level(pins.sda))return false;
    drive(pins.sda,false);drive(pins.scl,false);return true;
}
static bool stop_sccb(void) {
    drive(pins.sda,false);bool ok=high_clock();drive(pins.sda,true);return ok && level(pins.sda);
}
static bool send_byte(uint8_t value) {
    for(unsigned i=0;i<8;i++){
        drive(pins.sda,(value&0x80)!=0);if(!high_clock())return false;
        drive(pins.scl,false);value<<=1;
    }
    drive(pins.sda,true);if(!high_clock())return false;
    bool ack=!level(pins.sda);drive(pins.scl,false);return ack;
}
static int receive_byte(void) {
    int value=0;drive(pins.sda,true);
    for(unsigned i=0;i<8;i++){if(!high_clock())return -1;value=(value<<1)|level(pins.sda);drive(pins.scl,false);}
    /* One byte, final NACK. */
    if(!high_clock())return -1;drive(pins.scl,false);return value;
}
static bool sccb_ready(uint8_t address) {
    uint64_t now=cam_hw_now();
    return touched && !fault && address==pins.address && now!=UINT64_MAX && now<init_deadline;
}
int SCCB_Write16(uint8_t address,uint16_t reg,uint8_t value) {
    if(!sccb_ready(address))return -1;
    bool ok=start_sccb() && send_byte(address<<1) && send_byte(reg>>8) && send_byte(reg) && send_byte(value);
    ok=stop_sccb() && ok;cam_hw_yield();if(!ok)fault=true;return ok?0:-1;
}
int SCCB_Read16(uint8_t address,uint16_t reg) {
    if(!sccb_ready(address))return -1;
    bool ok=start_sccb() && send_byte(address<<1) && send_byte(reg>>8) && send_byte(reg);
    ok=stop_sccb() && ok;int value=-1;
    if(ok && start_sccb() && send_byte((address<<1)|1))value=receive_byte();
    ok=stop_sccb() && ok;cam_hw_yield();if(!ok || value<0)fault=true;return fault?-1:value;
}
int xclk_timer_conf(int timer,int hz) {
    (void)timer;if(!touched || hz!=20000000)return -1;
    CAM.cam_ctrl.cam_clkm_div_num=8;CAM.cam_ctrl.cam_clk_sel=3;CAM.cam_ctrl.cam_update=1;return 0;
}
static void input(unsigned pin,unsigned signal) {
    esp_rom_gpio_pad_select_gpio(pin);PADS.enable_w1tc=1u<<pin;
    /* All validated pads are GPIO4..18, contiguous IO_MUX registers. */
    REG_SET_BIT(IO_MUX_GPIO0_REG+4*pin,FUN_IE);
    REG_CLR_BIT(IO_MUX_GPIO0_REG+4*pin,FUN_PU|FUN_PD);
    esp_rom_gpio_connect_in_signal(pin,signal,false);
}
static bool valid_profile(const risc_camera_esp32s3_profile_v1 *p) {
    if(p->api_version!=1 || p->struct_size<sizeof(*p) || p->xclk_hz!=20000000 ||
       p->sensor_pid!=0x3660 || p->address!=0x3c || p->dma_channel>=5)return false;
    uint32_t used=0;uint8_t all[14];memcpy(all,p->data,8);
    all[8]=p->pclk;all[9]=p->vsync;all[10]=p->href;all[11]=p->xclk;all[12]=p->sda;all[13]=p->scl;
    for(unsigned i=0;i<14;i++){
        /* Qualified dedicated camera pad range excludes flash/PSRAM, bootstrap
         * SD14/21/47/48, ROM UART43/44 and USB19/20. GPIO14 is SD CS. */
        unsigned pin=all[i];if(pin<4 || pin>18 || pin==14 || (used&(1u<<pin)))return false;
        used|=1u<<pin;
    }
    return true;
}
bool cam_hw_start(const risc_camera_esp32s3_profile_v1 *p) {
    if(touched || buffer || !valid_profile(p))return false;
    /* Trusted runtime profile is exclusive; refuse an already enabled LCD_CAM
     * or occupied GDMA RX before any writes. Never reset the whole DMA unit. */
    if(REG_GET_BIT(SYSTEM_PERIP_CLK_EN1_REG,SYSTEM_LCD_CAM_CLK_EN) ||
       DMA.channel[p->dma_channel].in.link.addr || DMA.channel[p->dma_channel].in.int_ena.val)return false;
    pins=*p;fault=false;memset(&jpeg,0,sizeof(jpeg));
    buffer=heap_caps_malloc(FRAME_LIMIT,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    desc=heap_caps_calloc(NODES,sizeof(*desc),MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    if(!buffer || !desc)return false;
    uint64_t now=cam_hw_now();if(now==UINT64_MAX)return false;init_deadline=now+5000;
    touched=true;
    REG_SET_BIT(SYSTEM_PERIP_CLK_EN1_REG,SYSTEM_LCD_CAM_CLK_EN|SYSTEM_DMA_CLK_EN);
    REG_SET_BIT(SYSTEM_PERIP_RST_EN1_REG,SYSTEM_LCD_CAM_RST);
    REG_CLR_BIT(SYSTEM_PERIP_RST_EN1_REG,SYSTEM_LCD_CAM_RST);
    CAM.cam_ctrl.val=0;CAM.cam_ctrl1.val=0;CAM.cam_rgb_yuv.val=0;
    CAM.cam_ctrl.cam_stop_en=0;CAM.cam_ctrl.cam_vsync_filter_thres=4;
    CAM.cam_ctrl.cam_clkm_div_num=8;CAM.cam_ctrl.cam_clk_sel=3;
    /* Match native reference: byte-count EOF commits full DMA descriptors.
     * VSYNC is not a reliable frame EOF for the finite polled receiver. */
    CAM.cam_ctrl.cam_vs_eof_en=0;
    CAM.cam_ctrl1.cam_rec_data_bytelen=NODE_BYTES-1;CAM.cam_ctrl1.cam_vsync_filter_en=1;
    CAM.lc_dma_int_ena.val=0;CAM.lc_dma_int_clr.val=~0u;
    for(unsigned i=0;i<8;i++)input(pins.data[i],CAM_DATA_IN0_IDX+i);
    input(pins.pclk,CAM_PCLK_IDX);input(pins.vsync,CAM_V_SYNC_IDX);input(pins.href,CAM_H_ENABLE_IDX);
    /* The qualified Espressif OV3660 path uses vsync_invert=true. The
     * sensor pulse must be inverted at the matrix before CAM frame gating. */
    esp_rom_gpio_connect_in_signal(pins.vsync,CAM_V_SYNC_IDX,true);
    esp_rom_gpio_pad_select_gpio(pins.xclk);PADS.enable_w1ts=1u<<pins.xclk;
    esp_rom_gpio_connect_out_signal(pins.xclk,CAM_CLK_IDX,false,false);
    for(unsigned i=0;i<2;i++){
        unsigned pin=i?pins.scl:pins.sda;esp_rom_gpio_pad_select_gpio(pin);
        esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);
        PADS.out_w1tc=1u<<pin;PADS.enable_w1tc=1u<<pin;
        REG_SET_BIT(IO_MUX_GPIO0_REG+4*pin,FUN_IE|FUN_PU);
    }
    CAM.cam_ctrl.cam_update=1;
    unsigned ch=pins.dma_channel;dma_owned=true;
    DMA.channel[ch].in.int_ena.val=0;DMA.channel[ch].in.int_clr.val=~0u;
    DMA.channel[ch].in.conf0.val=0;DMA.channel[ch].in.conf0.in_rst=1;DMA.channel[ch].in.conf0.in_rst=0;
    DMA.channel[ch].in.conf0.indscr_burst_en=1;DMA.channel[ch].in.conf0.in_data_burst_en=1;
    DMA.channel[ch].in.conf1.in_check_owner=1;DMA.channel[ch].in.peri_sel.sel=5;
    memset(&sensor,0,sizeof(sensor));sensor.slv_addr=pins.address;sensor.xclk_freq_hz=pins.xclk_hz;
    cam_hw_yield();
    if(SCCB_Read16(pins.address,0x300a)!=0x36 || SCCB_Read16(pins.address,0x300b)!=0x60)return false;
    ov3660_init(&sensor);
    if(sensor.reset(&sensor) || sensor.set_pixformat(&sensor,PIXFORMAT_JPEG) ||
       sensor.set_framesize(&sensor,FRAMESIZE_SVGA) || sensor.set_quality(&sensor,12) || fault)return false;
    return true;
}
bool cam_hw_stop_capture(void) {
    waiting=false;
    if(!dma_owned){active=false;return true;}
    CAM.cam_ctrl1.cam_start=0;
    unsigned ch=pins.dma_channel;DMA.channel[ch].in.link.stop=1;
    /* No wait inside the provider's 2ms poll budget. A pending stop is retried
     * on later owner ticks; all buffers and claims remain pinned meanwhile. */
    __asm__ volatile("memw" ::: "memory");
    if(DMA.channel[ch].in.link.park){active=false;return true;}
    return false;
}
bool cam_hw_begin(unsigned quality) {
    if(!touched || fault || active || !cam_hw_stop_capture())return false;
    uint64_t now=cam_hw_now();if(now==UINT64_MAX)return false;init_deadline=now+500;
    if(sensor.set_quality(&sensor,quality) || fault)return false;
    memset(buffer,0,FRAME_LIMIT); /* 96KiB fixed bound, once per capture. */
    for(unsigned i=0;i<NODES;i++){
        memset(&desc[i],0,sizeof(desc[i]));desc[i].size=NODE_BYTES;desc[i].owner=1;
        desc[i].buf=buffer+i*NODE_BYTES;desc[i].qe.stqe_next=i+1<NODES?&desc[i+1]:NULL;
    }
    memset(&jpeg,0,sizeof(jpeg));available=0;harvested=0;CAM.lc_dma_int_clr.val=~0u;
    sampled_gpio=PADS.in;sampled_changes=0;
    /* With DMA parked, FIFO fills before a later VSYNC can be observed. Do
     * not auto-stop sampling on that expected overflow. No DMA can write yet;
     * the FIFO is reset before the finite descriptor chain is armed below. */
    CAM.cam_ctrl.cam_stop_en=0;
    /* Allow VSYNC observation while DMA is disabled. The next observed boundary
     * starts a finite capture; polling latency may truncate a frame, in which
     * case strict SOI/EOI validation fails instead of publishing junk. */
    CAM.cam_ctrl.cam_update=1;CAM.cam_ctrl1.cam_start=1;waiting=true;return true;
}
int32_t cam_hw_poll(const uint8_t **bytes,uint32_t *length) {
    *bytes=NULL;*length=0;unsigned ch=pins.dma_channel;
    uint32_t sampled=PADS.in;
    sampled_changes|=(sampled^sampled_gpio)&((1u<<pins.vsync)|(1u<<pins.href)|(1u<<pins.pclk));
    sampled_gpio=sampled;
    if(waiting){
        if(!CAM.lc_dma_int_raw.cam_vsync_int_raw)return T5_STREAM_AGAIN;
        CAM.cam_ctrl1.cam_start=0;CAM.lc_dma_int_clr.val=~0u;
        CAM.cam_ctrl1.cam_reset=1;CAM.cam_ctrl1.cam_reset=0;
        CAM.cam_ctrl1.cam_afifo_reset=1;CAM.cam_ctrl1.cam_afifo_reset=0;
        DMA.channel[ch].in.conf0.in_rst=1;DMA.channel[ch].in.conf0.in_rst=0;
        DMA.channel[ch].in.int_clr.val=~0u;
        DMA.channel[ch].in.link.addr=((uintptr_t)desc)&0xfffff;
        __asm__ volatile("memw" ::: "memory");
        DMA.channel[ch].in.link.start=1;
        CAM.cam_ctrl.cam_stop_en=1;CAM.cam_ctrl.cam_update=1;CAM.cam_ctrl1.cam_start=1;
        /* Same resynchronization as ll_cam_do_vsync; ccount edges provide
         * >=10us without a new delay/ISR import. Polled start can be late,
         * so discard a partial prefix and require an entire SOI..EOI JPEG. */
        esp_rom_gpio_connect_in_signal(pins.vsync,CAM_V_SYNC_IDX,false);
        edge();edge();
        esp_rom_gpio_connect_in_signal(pins.vsync,CAM_V_SYNC_IDX,true);
        waiting=false;active=true;return T5_STREAM_AGAIN;
    }
    if(jpeg.done){
        if(!cam_hw_stop_capture())return T5_STREAM_AGAIN;
        *bytes=buffer+jpeg.soi;*length=jpeg.length;return T5_STREAM_OK;
    }
    if(!active)return T5_STREAM_IO;
    if(DMA.channel[ch].in.int_raw.in_dscr_err)return T5_STREAM_IO;
    /* Only acquire completed, CPU-owned descriptors. This finite chain is
     * never rearmed while scanning; DMA cannot revisit or overwrite them.
     * Harvest <=4KiB and scan <=512 bytes per owner tick. A full-chain empty
     * interrupt is expected exhaustion, not corruption of committed bytes. */
    for(unsigned n=0;n<4 && harvested<NODES;n++){
        volatile lldesc_t *node=&desc[harvested];
        if(node->owner)break;
        __asm__ volatile("memw" ::: "memory");
        if(node->length!=NODE_BYTES)return T5_STREAM_IO;
        available+=NODE_BYTES;harvested++;
    }
    cam_jpeg_scan_step(&jpeg,buffer,available);
    if(jpeg.done){
        if(!cam_hw_stop_capture())return T5_STREAM_AGAIN;
        *bytes=buffer+jpeg.soi;*length=jpeg.length;return T5_STREAM_OK;
    }
    return harvested==NODES && jpeg.scan==available?T5_STREAM_LIMIT:T5_STREAM_AGAIN;
}
bool cam_hw_shutdown(void) {
    bool parked=cam_hw_stop_capture();
    const uint64_t began=cam_hw_now();
    for(unsigned retries=0;!parked && retries<25;retries++){
        uint64_t now=cam_hw_now();
        if(now==UINT64_MAX || began==UINT64_MAX || now<began || now-began>=25)break;
        cam_hw_yield();parked=cam_hw_stop_capture();
    }
    if(!parked)return false; // retain mapping, buffers and claims on uncertainty
    if(touched){
        if(dma_owned){DMA.channel[pins.dma_channel].in.link.addr=0;DMA.channel[pins.dma_channel].in.int_clr.val=~0u;dma_owned=false;}
        CAM.cam_ctrl1.cam_start=0;CAM.cam_ctrl.cam_clk_sel=0;CAM.cam_ctrl.cam_update=1;
        esp_rom_gpio_connect_out_signal(pins.xclk,SIG_GPIO_OUT_IDX,false,false);
        PADS.out_w1tc=1u<<pins.xclk;PADS.enable_w1ts=1u<<pins.xclk;
        drive(pins.sda,true);drive(pins.scl,true);
        REG_CLR_BIT(SYSTEM_PERIP_CLK_EN1_REG,SYSTEM_LCD_CAM_CLK_EN);
        touched=false;
    }
    heap_caps_free(desc);heap_caps_free(buffer);desc=NULL;buffer=NULL;fault=false;memset(&jpeg,0,sizeof(jpeg));
    return true;
}

const char *cam_hw_wait_reason(void) {
    static char detail[64];
    /* Sampled changes are activity evidence only, never a frequency estimate.
     * Snapshot before deadline cleanup clears START; no new CPU imports. */
    cam_hw_format_detail(detail,waiting?"VSYNC":active?"DMA":"JPEG",
        CAM.cam_ctrl1.val,CAM.lc_dma_int_raw.val,
        DMA.channel[pins.dma_channel].in.int_raw.val,sampled_gpio,sampled_changes);
    return detail;
}
