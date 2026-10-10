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
#include "OwnerPump.h"
#include "portable/synopsys/dwc2/dwc2_type.h"
static bool healthy=true;static unsigned events;
void risc_msc_transport_fault(void){healthy=false;}
static unsigned timeout_count,last_timeout;
void risc_msc_timeout(uint32_t reason){++timeout_count;last_timeout=reason;}
bool risc_msc_transport_ok(void){return healthy;}
void dcd_event_handler(dcd_event_t const *event,bool in_isr){(void)event;(void)in_isr;++events;}
static void reg_set(volatile const uint32_t *reg,uint32_t value){*(volatile uint32_t *)(uintptr_t)reg=value;}
static dwc2_regs_t *model;
static unsigned probes,hardware_calls,pump_passes;
uint64_t risc_msc_now(void){return 0;}
bool risc_msc_command_pending(void){return true;}
void risc_msc_pump_report(uint32_t n){pump_passes=n;}
static bool delayed_mmio_ready(void){
 if(++probes==4)reg_set(&model->gintsts,GINTSTS_IEPINT);
 return (model->gintsts&model->gintmsk)!=0;
}
static void actual_hardware(void){++hardware_calls;dcd_int_handler(0);}
static void no_event_after_fault(void){assert(false);}
int main(void){
 void *mem=mmap((void *)(uintptr_t)0x60080000u,sizeof(dwc2_regs_t),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(mem!=MAP_FAILED);
 dwc2_regs_t *r=mem;memset(mem,0,sizeof(*r));r->gintmsk=GINTMSK_IEPINT;r->diepmsk=DIEPMSK_TOM;
 reg_set(&r->gintsts,GINTSTS_IEPINT);reg_set(&r->daint,1u<<1);r->epin[1].diepint=DIEPINT_TOC;
 dcd_int_handler(0);assert(!healthy&&!events);assert(r->epin[1].diepint==DIEPINT_TOC);assert(timeout_count==1 && last_timeout==2);
 puts("PASS actual DWC2 bulk IN timeout latches controller fault without completion");
 healthy=true;r->epin[1].diepint=DIEPINT_TOC|DIEPINT_XFRC;
 dcd_int_handler(0);assert(!healthy&&!events);assert(r->epin[1].diepint==DIEPINT_TOC);
 puts("PASS simultaneous timeout/completion never reports transfer success");
 healthy=true;model=r;reg_set(&r->gintsts,0);r->epin[1].diepint=DIEPINT_TOC;
 assert(risc_msc_owner_pump(delayed_mmio_ready,actual_hardware,no_event_after_fault)==1);
 assert(!healthy && probes==4 && hardware_calls==1 && pump_passes==1 && !events);
 puts("PASS initially empty active pump observes delayed masked MMIO event through actual DWC2 handler");
 munmap(mem,sizeof(*r));return 0;
}
