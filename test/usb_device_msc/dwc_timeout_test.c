/* Run the actual staged DWC2 handler against an isolated MMIO register image.
 * Register writes are inspected; FIFO/electrical timing is not simulated. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "tusb.h"
#include "device/dcd.h"
#include "portable/synopsys/dwc2/dwc2_type.h"
static bool healthy=true;static unsigned events;
void risc_msc_transport_fault(void){healthy=false;}
bool risc_msc_transport_ok(void){return healthy;}
void dcd_event_handler(dcd_event_t const *event,bool in_isr){(void)event;(void)in_isr;++events;}
static void reg_set(volatile const uint32_t *reg,uint32_t value){*(volatile uint32_t *)(uintptr_t)reg=value;}
int main(void){
 void *mem=mmap((void *)(uintptr_t)0x60080000u,sizeof(dwc2_regs_t),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(mem!=MAP_FAILED);
 dwc2_regs_t *r=mem;memset(mem,0,sizeof(*r));r->gintmsk=GINTMSK_IEPINT;r->diepmsk=DIEPMSK_TOM;
 reg_set(&r->gintsts,GINTSTS_IEPINT);reg_set(&r->daint,1u<<1);r->epin[1].diepint=DIEPINT_TOC;
 dcd_int_handler(0);assert(!healthy&&!events);assert(r->epin[1].diepint==DIEPINT_TOC);
 puts("PASS actual DWC2 bulk IN timeout latches controller fault without completion");
 healthy=true;r->epin[1].diepint=DIEPINT_TOC|DIEPINT_XFRC;
 dcd_int_handler(0);assert(!healthy&&!events);assert(r->epin[1].diepint==DIEPINT_TOC);
 puts("PASS simultaneous timeout/completion never reports transfer success");
 munmap(mem,sizeof(*r));return 0;
}
