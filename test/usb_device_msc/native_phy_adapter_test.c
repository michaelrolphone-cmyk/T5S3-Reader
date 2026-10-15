/* Exercise the build-only adapter with real pinned IDF6 register layouts. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <soc/system_struct.h>
#include <soc/rtc_cntl_struct.h>
#include <soc/usb_wrap_struct.h>
static system_dev_t system_regs;
static rtc_cntl_dev_t rtc_regs;
#define SYSTEM system_regs
#define RTCCNTL rtc_regs
#include <hal/usb_phy_ll.h>
int main(void)
{
    usb_wrap_dev_t wrap;
    for (unsigned value = 0; value < 256; ++value) {
        memset(&wrap, 0, sizeof(wrap));
        system_regs = (system_dev_t){0};
        rtc_regs = (rtc_cntl_dev_t){0};
        wrap.otg_conf.val = value * 0x01010101u;
        rtc_regs.usb_conf.val = ~wrap.otg_conf.val;
        uint32_t before = wrap.otg_conf.val;
        usb_wrap_dev_t expected = wrap;
        rtc_cntl_dev_t expected_rtc = rtc_regs;
        expected.otg_conf.phy_sel = 0;
        expected_rtc.usb_conf.sw_hw_usb_phy_sel = 1;
        expected_rtc.usb_conf.sw_usb_phy_sel = 1;
        usb_phy_ll_int_otg_enable(&wrap);
        assert(wrap.otg_conf.val == expected.otg_conf.val);
        assert(rtc_regs.usb_conf.val == expected_rtc.usb_conf.val);
        for (unsigned enabled = 0; enabled < 2; ++enabled) {
            wrap.otg_conf.val = before;
            expected = wrap;
            expected.otg_conf.pad_enable = enabled;
            usb_phy_ll_usb_wrap_pad_enable(&wrap, enabled);
            assert(wrap.otg_conf.val == expected.otg_conf.val);
            system_regs.perip_clk_en0.val = before;
            system_dev_t expected_system = system_regs;
            expected_system.perip_clk_en0.usb_clk_en = enabled;
            usb_phy_ll_usb_wrap_enable_bus_clock(enabled);
            assert(system_regs.perip_clk_en0.val == expected_system.perip_clk_en0.val);
        }
        system_regs.perip_rst_en0.val = before;
        system_dev_t expected_system = system_regs;
        expected_system.perip_rst_en0.usb_rst = 0;
        usb_phy_ll_usb_wrap_reset_register();
        assert(system_regs.perip_rst_en0.val == expected_system.perip_rst_en0.val);
    }
    puts("PASS IDF6 PHY adapter preserves all unrelated register fields (256 patterns)");
}
