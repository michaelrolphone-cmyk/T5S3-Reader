/* Production scheduling policy with full-speed packets arriving over time.
 * Timings are a deterministic model, not an electrical throughput benchmark. */
#include <assert.h>
#include <stdio.h>
#include "OwnerPump.h"
static uint64_t us,next_packet;
static unsigned packets,total,passes,reported,ready_calls;
static bool active,healthy=true,frozen_clock,rollback;
static unsigned now_calls,fault_at;
uint64_t risc_msc_now(void){++now_calls;if(rollback)return now_calls==1?5:4;if(!frozen_clock)++us;return us/1000;}
bool risc_msc_transport_ok(void){return healthy;}
bool risc_msc_command_pending(void){return active;}
void risc_msc_pump_report(uint32_t count){reported=count;}
static bool ready(void){++ready_calls;if(!frozen_clock)++us;return packets<total && us>=next_packet;}
static void hardware(void){assert(packets<total && us>=next_packet);++packets;next_packet=us+65;if(fault_at && packets==fault_at)healthy=false;}
static void event(void){++passes;if(packets==total)active=false;}
static void reset(void){us=next_packet=packets=passes=reported=ready_calls=0;total=0;active=false;healthy=true;frozen_clock=rollback=false;now_calls=fault_at=0;}
int main(void){
 reset();total=1024;active=true;unsigned polls=0;
 while(active && polls<1100){risc_msc_owner_pump(ready,hardware,event);++polls;us+=1000;}
 printf("64 KiB delayed-packet model packets=%u polls=%u elapsed_us=%llu\n",packets,polls,(unsigned long long)us);fflush(stdout);
 assert(packets==1024 && polls<=40 && us<120000);
 reset();assert(!risc_msc_owner_pump(ready,hardware,event));assert(ready_calls==1 && !passes);puts("PASS inactive idle does not busy-poll");
 reset();active=true;total=1;next_packet=3000;risc_msc_owner_pump(ready,hardware,event);assert(us>=2000 && us<2100 && !passes);puts("PASS host pause returns at 2 ms admission deadline");
 reset();active=true;total=1;next_packet=3000;frozen_clock=true;risc_msc_owner_pump(ready,hardware,event);assert(!passes && ready_calls==RISC_MSC_PUMP_MAX_PROBES);puts("PASS frozen clock has finite no-progress probe budget");
 reset();active=true;total=4;healthy=false;assert(!risc_msc_owner_pump(ready,hardware,event) && !ready_calls);puts("PASS controller fault admits no traffic");
 reset();active=true;total=4;rollback=true;assert(!risc_msc_owner_pump(ready,hardware,event) && !ready_calls && now_calls==2);puts("PASS clock rollback admits no work");
 reset();active=true;total=4;fault_at=2;assert(risc_msc_owner_pump(ready,hardware,event)==2 && packets==2 && passes==1 && !healthy);puts("PASS fault after delayed packet suppresses stack work and exits");
 return 0;
}
