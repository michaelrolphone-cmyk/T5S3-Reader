#include "RiscTouchPowerV1.h"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
static uint64_t subscribe(void *c) {(void)c;return 1;}
static bool unsubscribe(void *c,uint64_t s) {(void)c;(void)s;return true;}
static bool poll(void *c,size_t n) {(void)c;(void)n;return true;}
static int32_t next(void *c,uint64_t s,risc_touch_event_v1 *e) {(void)c;(void)s;(void)e;return 0;}
static bool snapshot(void *c,risc_touch_snapshot_v1 *s) {(void)c;(void)s;return true;}
static int32_t prepare(void *c,uint32_t t) {(void)c;(void)t;return RISC_TOUCH_POWER_OK;}
int main(void) {
    assert(offsetof(risc_touch_power_api_v1,base)==0);
    assert(offsetof(risc_touch_power_api_v1,power_tag)==sizeof(risc_touch_api_v1));
    risc_touch_api_v1 legacy={1,sizeof(legacy),NULL,subscribe,unsubscribe,poll,next,snapshot};
    assert(!risc_touch_power(NULL) && !risc_touch_power(&legacy));
    risc_touch_power_api_v1 p={legacy,RISC_TOUCH_POWER_TAG,1,prepare,prepare};
    p.base.struct_size=sizeof(p);assert(risc_touch_power(&p.base)==&p);
    assert(!memcmp(&p.base.context,&legacy.context,sizeof(legacy)-offsetof(risc_touch_api_v1,context)));
    for(uint32_t n=0;n<sizeof(p);++n){p.base.struct_size=n;assert(!risc_touch_power(&p.base));}
    p.base.struct_size=sizeof(p);p.base.api_version=2;assert(!risc_touch_power(&p.base));p.base.api_version=1;
    p.power_tag^=1;assert(!risc_touch_power(&p.base));p.power_tag^=1;
    p.power_version=2;assert(!risc_touch_power(&p.base));p.power_version=1;
    p.prepare=NULL;assert(!risc_touch_power(&p.base));p.prepare=prepare;
    p.resume=NULL;assert(!risc_touch_power(&p.base));p.resume=prepare;
    p.base.subscribe=NULL;assert(!risc_touch_power(&p.base));p.base.subscribe=subscribe;
    p.base.unsubscribe=NULL;assert(!risc_touch_power(&p.base));p.base.unsubscribe=unsubscribe;
    p.base.poll=NULL;assert(!risc_touch_power(&p.base));p.base.poll=poll;
    p.base.next=NULL;assert(!risc_touch_power(&p.base));p.base.next=next;
    p.base.snapshot=NULL;assert(!risc_touch_power(&p.base));p.base.snapshot=snapshot;
    assert(risc_touch_power(&p.base)->prepare(p.base.context,0)==RISC_TOUCH_POWER_OK);
    assert(risc_touch_power(&p.base)->resume(p.base.context,1000)==RISC_TOUCH_POWER_OK);
    puts("Touch power suffix: legacy prefix, tag/version/size/function checks PASS");
}
