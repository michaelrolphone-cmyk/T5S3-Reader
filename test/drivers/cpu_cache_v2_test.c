#include "RiscCpuCacheV2.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
static bool isr,locked,deny_lock;
static unsigned calls,yields,fail_call,cost;
static int64_t now;
static uintptr_t expected;
int xPortInIsrContext(void){return isr;}
int64_t esp_timer_get_time(void){return now;}
int cache_try_lock(void){assert(!locked);if(deny_lock)return 0;locked=true;return 1;}
void cache_unlock(void){assert(locked);locked=false;}
void vTaskDelay(unsigned ticks){assert(!locked && ticks==1);++yields;now+=1000;}
int Cache_WriteBack_Addr(uintptr_t address,size_t bytes){
 assert(locked && bytes>0 && bytes<=4096 && address==expected);
 expected+=bytes;now+=cost;++calls;return calls==fail_call?-1:0;
}
static void reset(void){isr=locked=deny_lock=false;calls=yields=fail_call=cost=0;now=0;expected=0x3d000001;}
int main(void){
 reset();isr=true;assert(risc_cpu_cache_writeback_v2(expected,1)==-2);isr=false;
 assert(risc_cpu_cache_writeback_v2(expected,0)==-1);
 assert(risc_cpu_cache_writeback_v2(expected,1048577)==-1);
 assert(risc_cpu_cache_writeback_v2(UINTPTR_MAX-3,8)==-1);
 assert(risc_cpu_cache_writeback_v2(0x3dffffff,2)==-1);
 assert(risc_cpu_cache_writeback_v2(0x3cffffff,1)==-1);
 assert(risc_cpu_cache_writeback_v2(0x3c000001,1)==-1);
 assert(!calls);
 assert(!risc_cpu_cache_writeback_v2(expected,1048576));assert(calls==256 && yields==31);
 reset();cost=2100;assert(!risc_cpu_cache_writeback_v2(expected,8193));assert(calls==3 && yields==2);
 reset();fail_call=2;assert(risc_cpu_cache_writeback_v2(expected,16384)==-4 && calls==2 && !locked);
 reset();cost=100001;assert(risc_cpu_cache_writeback_v2(expected,1)==-5 && calls==1);
 reset();deny_lock=true;assert(risc_cpu_cache_writeback_v2(expected,4096)==-5 && !calls && yields==100);
 reset();expected=0x3dffffff;assert(!risc_cpu_cache_writeback_v2(expected,1));
 puts("CPU cache: exclusive bounds, ISR rejection, chunk/time yields, deadline and failure propagation PASS");
}
