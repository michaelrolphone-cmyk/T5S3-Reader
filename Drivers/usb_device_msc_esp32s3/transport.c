/* ESP32-S3 transport uses the shared controller's PHY route preservation.
 * Native lease owns exclusion with HWCDC; all protocol work stays this ELF. */
#include <stdbool.h>
#include <stdint.h>
#include <soc/system_struct.h>
#include <soc/usb_wrap_struct.h>
#include <soc/rtc_cntl_struct.h>
#include <hal/usb_phy_ll.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_pins.h>
#include <soc/io_mux_reg.h>
#include "tusb.h"
#include "device/dcd.h"
#include "portable/synopsys/dwc2/dwc2_type.h"
#include "Transport.h"
#include "../usb_controller_esp32s3/PhyRoute.h"
static bool healthy=true,live,saved_clock;
static uint32_t saved_wrap,saved_test,saved_inputs[4];
static uint8_t saved_drive[2];
static const uint16_t input_signal[4]={USB_OTG_IDDIG_IN_IDX,USB_SRP_BVALID_IN_IDX,USB_OTG_VBUSVALID_IN_IDX,USB_OTG_AVALID_IN_IDX};
static void device_inputs(void) {
 /* Same fixed device-role signal routing as IDF v4.4.7 usb_phy_otg_set_mode.
  * Preserve all four original matrix selectors until checked controller stop. */
 for(unsigned i=0;i<4;++i) {
  saved_inputs[i]=GPIO.func_in_sel_cfg[input_signal[i]].val;
  GPIO.func_in_sel_cfg[input_signal[i]].func_sel=i==3?GPIO_MATRIX_CONST_ZERO_INPUT:GPIO_MATRIX_CONST_ONE_INPUT;
  GPIO.func_in_sel_cfg[input_signal[i]].sig_in_sel=1;
  GPIO.func_in_sel_cfg[input_signal[i]].sig_in_inv=0;
 }
}
static dwc2_regs_t *regs(void) { return (dwc2_regs_t *)(uintptr_t)0x60080000u; }
void risc_msc_transport_fault(void) { healthy=false; }
bool risc_msc_transport_ok(void) { return healthy; }
static bool wait_bits(volatile uint32_t *reg,uint32_t mask,bool set) {
 for(unsigned i=0;i<100000u;++i)if(((*reg&mask)!=0)==set)return true;
 healthy=false;return false;
}
bool risc_msc_transport_start(void) {
 if(live || phyRouteCaptured)return false;
 healthy=true;capture_phy_route();
 saved_clock=SYSTEM.perip_clk_en0.usb_clk_en;
 saved_wrap=USB_WRAP.otg_conf.val;saved_test=USB_WRAP.test_conf.val;
 saved_drive[0]=REG_GET_FIELD(IO_MUX_GPIO19_REG,FUN_DRV);saved_drive[1]=REG_GET_FIELD(IO_MUX_GPIO20_REG,FUN_DRV);
 live=true; /* Every failed path still requires checked stop. */
 device_inputs();
 usb_phy_ll_usb_wrap_enable_bus_clock(true);
 usb_phy_ll_usb_wrap_reset_register();
 usb_phy_ll_int_otg_enable(&USB_WRAP);
 /* Let the OTG device controller drive its own D+ pullup. */
 USB_WRAP.otg_conf.pad_pull_override=0;
 usb_phy_ll_usb_wrap_pad_enable(&USB_WRAP,true);
 if(gpio_set_drive_capability(19,GPIO_DRIVE_CAP_3)!=ESP_OK ||
    gpio_set_drive_capability(20,GPIO_DRIVE_CAP_3)!=ESP_OK){healthy=false;return false;}
 dwc2_regs_t *r=regs();
 const uint32_t id=r->gsnpsid&GSNPSID_ID_MASK;
 if(id!=DWC2_OTG_ID && id!=DWC2_FS_IOT_ID && id!=DWC2_HS_IOT_ID){healthy=false;return false;}
 /* Slave FIFO only. No native or ELF buffer is handed to DMA. */
 r->gahbcfg&=~GAHBCFG_DMAEN;
 if(!tud_init(0) || !healthy)return false;
 r->gahbcfg&=~(GAHBCFG_GINT|GAHBCFG_DMAEN);
 return !(r->gintsts&GINTSTS_CMOD) && healthy;
}
bool risc_msc_transport_poll(void) {
 if(!live || !healthy)return false;
 if(regs()->gintsts&GINTSTS_MMIS){healthy=false;return false;}
 /* Hardware interrupt handler is invoked synchronously, never registered as
  * an ISR. One bounded hardware pass then at most32 queued stack events. */
 dcd_int_handler(0);
 if(healthy)tud_task_ext(0,false);
 return healthy;
}
bool risc_msc_transport_stop(void) {
 if(!live)return !phyRouteCaptured;
 dwc2_regs_t *r=regs();
 r->dctl|=DCTL_SDIS;
 r->gintmsk=0;r->gahbcfg&=~(GAHBCFG_GINT|GAHBCFG_DMAEN);
 if(!(r->dctl&DCTL_SDIS))return false;
 r->grstctl|=GRSTCTL_CSRST;
 if(!wait_bits(&r->grstctl,GRSTCTL_CSRST,false) || !wait_bits(&r->grstctl,GRSTCTL_AHBIDL,true))return false;
 /* Core reset proves no endpoint/fifo transaction survives. No ISR or task
  * exists, so clearing the stack now cannot discard an in-flight callback. */
 risc_msc_stack_reset();
 if(gpio_set_drive_capability(19,(gpio_drive_cap_t)saved_drive[0])!=ESP_OK ||
    gpio_set_drive_capability(20,(gpio_drive_cap_t)saved_drive[1])!=ESP_OK)return false;
 if(REG_GET_FIELD(IO_MUX_GPIO19_REG,FUN_DRV)!=saved_drive[0] || REG_GET_FIELD(IO_MUX_GPIO20_REG,FUN_DRV)!=saved_drive[1])return false;
 USB_WRAP.otg_conf.val=saved_wrap;USB_WRAP.test_conf.val=saved_test;
 for(unsigned i=0;i<4;++i)GPIO.func_in_sel_cfg[input_signal[i]].val=saved_inputs[i];
 restore_phy_route();
 if(USB_WRAP.otg_conf.val!=saved_wrap || USB_WRAP.test_conf.val!=saved_test ||
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel!=savedPhyOverride || RTCCNTL.usb_conf.sw_usb_phy_sel!=savedPhySelect) {
  phyRouteCaptured=true;return false;
 }
 for(unsigned i=0;i<4;++i)if(GPIO.func_in_sel_cfg[input_signal[i]].val!=saved_inputs[i]){phyRouteCaptured=true;return false;}
 usb_phy_ll_usb_wrap_enable_bus_clock(saved_clock);
 if(SYSTEM.perip_clk_en0.usb_clk_en!=saved_clock){phyRouteCaptured=true;return false;}
 live=false;healthy=true;return true;
}
