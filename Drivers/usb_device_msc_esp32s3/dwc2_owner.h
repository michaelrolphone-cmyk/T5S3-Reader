#pragma once
/* The shared device stack executes only within its provider's owner-task poll.
 * Never installs an ISR, creates a task, or enables DMA into movable ELF RAM. */
#include "Transport.h"
#define DWC2_EP_MAX 6
static const dwc2_controller_t _dwc2_controller[] = {
 { .reg_base=0x60080000u, .irqnum=0, .ep_count=6, .ep_fifo_size=1024 }
};
static inline void dwc2_dcd_int_enable(uint8_t port) { (void)port; }
static inline void dwc2_dcd_int_disable(uint8_t port) { (void)port; }
static inline void dwc2_remote_wakeup_delay(void) { risc_msc_transport_fault(); }
static inline void dwc2_phy_init(dwc2_regs_t *r,uint8_t t) { (void)r;(void)t; }
static inline void dwc2_phy_update(dwc2_regs_t *r,uint8_t t) { (void)r;(void)t; }
/* Every controller hardware wait has an explicit CPU-poll budget. No timeout
 * can be interpreted as successful reset/endpoint retirement. */
static inline bool risc_msc_wait(volatile uint32_t *reg,uint32_t mask,bool set) {
 for(unsigned n=0;n<100000u;++n) if (((*reg & mask)!=0)==set) return true;
 risc_msc_timeout(1u);risc_msc_transport_fault();return false;
}
