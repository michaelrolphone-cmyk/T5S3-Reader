/* Compile the production trampoline. The kernel stub suspends each old worker
 * immediately after completion publication, while the creator consumes and
 * reuses its descriptor. Resuming the old resident tail must not touch the new
 * entry, argument, or completion state. No test-only production hooks. */
#include "RiscCpuWorkerV2.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

static _Thread_local uintptr_t current=1;
static _Thread_local bool in_isr;
static _Thread_local unsigned worker_index;
static atomic_uint ticks, entries;
static bool fail_create, finish_before_return;
static unsigned created;
struct task { pthread_t thread; void (*entry)(void*); void *argument;
 atomic_bool tail, resume; } tasks[32];
TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void*)current;}
int xPortInIsrContext(void){return in_isr;}
TickType_t xTaskGetTickCount(void){return atomic_load(&ticks);}
void vTaskDelay(TickType_t n){atomic_fetch_add(&ticks,n);sched_yield();}
void vTaskDelete(TaskHandle_t task){
 assert(!task);struct task *t=&tasks[worker_index];
 atomic_store(&t->tail,true);
 while(!atomic_load(&t->resume))sched_yield();
 pthread_exit(NULL);
}
static void *run(void *p){
 unsigned n=(unsigned)(uintptr_t)p;worker_index=n;current=100+n;
 tasks[n].entry(tasks[n].argument);assert(!"resident entry returned");return NULL;
}
int xTaskCreatePinnedToCore(void (*entry)(void*),const char *name,uint32_t stack,
 void *argument,uint32_t priority,TaskHandle_t *out,int core){
 assert(!strcmp(name,"rte-worker") && stack>=4096 && priority<=5 && !out && core>=-1);
 if(fail_create)return 0;
 unsigned n=created++;assert(n<32);
 tasks[n].entry=entry;tasks[n].argument=argument;
 assert(!pthread_create(&tasks[n].thread,NULL,run,(void*)(uintptr_t)n));
 if(finish_before_return)while(!atomic_load(&tasks[n].tail))sched_yield();
 return pdPASS;
}
static void count(void *p){assert(p==(void*)42);atomic_fetch_add(&entries,1);}
static atomic_bool entry_gate;
static risc_cpu_worker_v2 self_handle;
static void blocked(void *p){
 (void)p;while(!atomic_load(&entry_gate))sched_yield();
 assert(risc_cpu_worker_join_v2(self_handle,0)==RISC_CPU_WORKER_CONTEXT);
 assert(risc_cpu_worker_release_v2(self_handle)==RISC_CPU_WORKER_CONTEXT);
 atomic_fetch_add(&entries,1);
}
static risc_cpu_worker_v2 start(void (*entry)(void*)){
 risc_cpu_worker_v2 h=0;assert(!risc_cpu_worker_start_v2(entry,(void*)42,4096,1,-1,&h));
 assert(h);return h;
}
static void resume(unsigned n){atomic_store(&tasks[n].resume,true);assert(!pthread_join(tasks[n].thread,NULL));}
int main(int argc,char **argv){
 assert(argc==2);
 risc_cpu_worker_v2 h=99;
 assert(risc_cpu_worker_start_v2(NULL,NULL,4096,1,0,&h)==RISC_CPU_WORKER_INVALID && !h);
 assert(risc_cpu_worker_start_v2(count,NULL,4095,1,0,&h)==RISC_CPU_WORKER_INVALID);
 assert(risc_cpu_worker_start_v2(count,NULL,32772,1,0,&h)==RISC_CPU_WORKER_INVALID);
 assert(risc_cpu_worker_start_v2(count,NULL,4097,1,0,&h)==RISC_CPU_WORKER_INVALID);
 assert(risc_cpu_worker_start_v2(count,NULL,4096,6,0,&h)==RISC_CPU_WORKER_INVALID);
 assert(risc_cpu_worker_start_v2(count,NULL,4096,1,2,&h)==RISC_CPU_WORKER_INVALID);
 in_isr=true;
 assert(risc_cpu_worker_start_v2(count,NULL,4096,1,0,&h)==RISC_CPU_WORKER_CONTEXT);
 assert(risc_cpu_worker_join_v2(1,0)==RISC_CPU_WORKER_CONTEXT);
 assert(risc_cpu_worker_release_v2(1)==RISC_CPU_WORKER_CONTEXT);in_isr=false;
 fail_create=true;
 assert(risc_cpu_worker_start_v2(count,NULL,4096,1,0,&h)==RISC_CPU_WORKER_ALLOCATION && !h);
 assert(!created && !atomic_load(&entries));fail_create=false;
 finish_before_return=true;
 void *image=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(image);
 void (*provider_entry)(void*)=(void(*)(void*))dlsym(image,"provider_entry");assert(provider_entry);
 size_t page=(size_t)sysconf(_SC_PAGESIZE);
 atomic_uint **argument=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(argument!=MAP_FAILED);*argument=&entries;
 assert(!risc_cpu_worker_start_v2(provider_entry,argument,4096,1,-1,&h));
 assert(atomic_load(&entries)==1);
 assert(!risc_cpu_worker_join_v2(h,0));assert(!risc_cpu_worker_join_v2(h,100));
 current=2;assert(risc_cpu_worker_release_v2(h)==RISC_CPU_WORKER_CONTEXT);current=1;
 assert(!risc_cpu_worker_release_v2(h));
 /* Provider code and its argument are gone before the suspended resident
  * cleanup resumes, with the descriptor already available to its successor. */
 assert(!dlclose(image));assert(!munmap(argument,page));
 assert(risc_cpu_worker_release_v2(h)==RISC_CPU_WORKER_STALE);
 finish_before_return=false;
 self_handle=start(blocked);
 assert((h&255)==(self_handle&255) && h!=self_handle);
 assert(risc_cpu_worker_join_v2(h,0)==RISC_CPU_WORKER_STALE);
 assert(risc_cpu_worker_join_v2(self_handle,0)==RISC_CPU_WORKER_TIMEOUT);
 assert(risc_cpu_worker_join_v2(self_handle,2001)==RISC_CPU_WORKER_INVALID);
 assert(risc_cpu_worker_release_v2(self_handle)==RISC_CPU_WORKER_BUSY);
 unsigned before=atomic_load(&ticks);
 assert(risc_cpu_worker_join_v2(self_handle,2000)==RISC_CPU_WORKER_TIMEOUT);
 assert(atomic_load(&ticks)-before==2000);
 /* Resume the old tail with the very same descriptor now holding new work. */
 resume(0);
 assert(risc_cpu_worker_join_v2(self_handle,0)==RISC_CPU_WORKER_TIMEOUT);
 assert(atomic_load(&entries)==1);
 atomic_store(&entry_gate,true);
 while(!atomic_load(&tasks[1].tail))sched_yield();
 assert(!risc_cpu_worker_join_v2(self_handle,0));assert(!risc_cpu_worker_release_v2(self_handle));resume(1);
 finish_before_return=true;
 risc_cpu_worker_v2 held[4];for(unsigned n=0;n<4;++n)held[n]=start(count);
 assert(risc_cpu_worker_start_v2(count,NULL,4096,1,0,&h)==RISC_CPU_WORKER_CAPACITY && !h);
 for(unsigned n=0;n<4;++n){assert(!risc_cpu_worker_release_v2(held[n]));resume(n+2);}
 assert(atomic_load(&entries)==6);
 puts("CPU worker: post-publication reuse race, early completion, ownership, failure, capacity and bounded join PASS");
}
