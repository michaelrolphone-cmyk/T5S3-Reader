/* Production owner-pump policy with deterministic packet-ready hardware.
 * This models scheduling and exact work counts, not electrical USB timing. */
#include <assert.h>
#include <stdio.h>
#include "OwnerPump.h"
static uint64_t now_ms;
static unsigned packets,pending,hardware_calls,event_calls,report_calls,reported;
static unsigned elapsed_per_pass,fault_after;
static bool healthy=true,never_idle;
uint64_t risc_msc_now(void){return now_ms;}
bool risc_msc_transport_ok(void){return healthy;}
void risc_msc_pump_report(uint32_t passes){++report_calls;reported=passes;}
static bool ready(void){return pending||never_idle;}
static void hardware(void){++hardware_calls;if(pending){--pending;++packets;}if(fault_after&&hardware_calls==fault_after)healthy=false;}
static void event(void){++event_calls;now_ms+=elapsed_per_pass;}
static void reset(void){now_ms=packets=pending=hardware_calls=event_calls=report_calls=reported=elapsed_per_pass=fault_after=0;healthy=true;never_idle=false;}
int main(void){
 reset();pending=8;unsigned old_polls=0;while(ready()){hardware();event();++old_polls;}assert(packets==8&&old_polls==8);
 reset();pending=8;assert(risc_msc_owner_pump(ready,hardware,event)==8);assert(packets==8&&hardware_calls==8&&event_calls==8&&reported==8&&report_calls==1);
 printf("PASS sector scheduling model: 8 ready packets required %u old polls; new pump requires 1 poll, 8 passes\n",old_polls);
 reset();assert(!risc_msc_owner_pump(ready,hardware,event));assert(!hardware_calls&&!event_calls&&!reported&&report_calls==1);puts("PASS idle returns with zero hardware/events");
 reset();never_idle=true;assert(risc_msc_owner_pump(ready,hardware,event)==32);assert(hardware_calls==32&&event_calls==32);puts("PASS continuous-ready capped at 32 passes");
 reset();never_idle=true;elapsed_per_pass=1;assert(risc_msc_owner_pump(ready,hardware,event)==2);assert(now_ms==2&&hardware_calls==2);puts("PASS elapsed admission budget stops at 2 ms");
 reset();pending=8;elapsed_per_pass=9;assert(risc_msc_owner_pump(ready,hardware,event)==1);assert(now_ms==9&&pending==7);puts("PASS slow in-flight callback admits no further pass");
 reset();pending=8;fault_after=1;assert(risc_msc_owner_pump(ready,hardware,event)==1);assert(!healthy&&hardware_calls==1&&!event_calls&&pending==7);puts("PASS hardware fault suppresses stack work");
 reset();pending=8;healthy=false;assert(!risc_msc_owner_pump(ready,hardware,event));assert(!hardware_calls&&!event_calls&&pending==8);puts("PASS pre-existing controller fault admits zero work");
 return 0;
}
