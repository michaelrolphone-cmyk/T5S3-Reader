#!/usr/bin/env python3
"""Count real GT911/consumer work at a fixed Reader checkout, no hardware."""
import argparse, ast, hashlib, json, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
R=a.checkout.resolve();D=a.output.resolve();D.mkdir(parents=True,exist_ok=True)
source=(R/'src/native/NativeTouchInput.cpp').read_text()
original_test=(R/'test/hal/touch_capture_behavior_test.py').read_text()
literals={}
for node in ast.parse(original_test).body:
 if isinstance(node,ast.Assign) and isinstance(node.value,ast.Constant) and isinstance(node.value.value,str):
  for target in node.targets:
   if isinstance(target,ast.Name):literals[target.id]=node.value.value
prefix=literals['prefix'];core=source[source.index('namespace {'):source.index('bool workerShouldRun()')];getters=source[source.index('void nativeTouchDiscardGestures('):]
bus=(R/'test/drivers/gt911_touch_test.c').read_text().split('int main(void)')[0]
bus=bus.replace('static bool transact(', 'static unsigned transfers, statusReads, pointReads, acknowledgements, productReads;\nstatic bool transact(')
bus=bus.replace('    assert(claim == active_claim);','    ++transfers;\n    assert(claim == active_claim);')
for pattern,counter in [('    if (write_length == 3u && !read_length && reg == 0x814eu) {','acknowledgements'),('    if (reg == 0x8140u && read_length == 11u) {','productReads'),('    if (reg == 0x814eu && read_length == 1u) {','statusReads'),('    if (reg == 0x814fu && read_length <= sizeof(points)) {','pointReads')]:
 assert pattern in bus
 bus=bus.replace(pattern,pattern+'\n        ++'+counter+';')
test=r'''
static unsigned polls, nextCalls, snapshotCalls;
static const risc_touch_api_v1* actual;
static bool countPoll(void* c,size_t n){++polls;return actual->poll(c,n);}
static int32_t countNext(void* c,uint64_t t,risc_touch_event_v1* o){++nextCalls;return actual->next(c,t,o);}
static bool countSnapshot(void*c,risc_touch_snapshot_v1*o){++snapshotCalls;return actual->snapshot(c,o);}
static void zero(){transfers=statusReads=pointReads=acknowledgements=productReads=0;polls=nextCalls=snapshotCalls=0;}
static void emit(const char*name,unsigned delivered){
 printf("{\"case\":\"%s\",\"polls\":%u,\"next_calls\":%u,\"snapshot_calls\":%u,\"bus_calls\":%u,\"status_reads\":%u,\"point_reads\":%u,\"ack_writes\":%u,\"product_reads\":%u,\"delivered_taps\":%u}\n",name,polls,nextCalls,snapshotCalls,transfers,statusReads,pointReads,acknowledgements,productReads,delivered);
}
int main(){
 nowMs=1000;fake_ms=nowMs;
 const auto*driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
 assert(driver&&driver->start(dependencies,2));
 actual=static_cast<const risc_touch_api_v1*>(driver->capability);
 risc_touch_api_v1 wrapped=*actual;wrapped.poll=countPoll;wrapped.next=countNext;wrapped.snapshot=countSnapshot;
 api=&wrapped;subscription=api->subscribe(nullptr);assert(subscription&&resync(true));
 assert(transfers==2&&productReads==1&&acknowledgements==1);emit("start",0);
 NativeTouchPoint p{};zero();
 for(unsigned i=0;i<1000;++i){nowMs+=5;fake_ms=nowMs;serviceProvider();assert(!nativeTouchGetTap(p));}
 assert(polls==1000&&transfers==1000&&nextCalls==1000&&snapshotCalls==0);emit("1000_idle_capture_turns",0);
 zero();unsigned delivered=0;
 for(unsigned tap=0;tap<100;++tap){
  nowMs+=100;fake_ms=nowMs;report_one(3,100,200);serviceProvider();
  nowMs+=50;fake_ms=nowMs;report_release();serviceProvider();
  assert(nativeTouchGetTap(p)&&p.x==100&&p.y==200);++delivered;assert(!nativeTouchGetTap(p));
 }
 assert(polls==200&&transfers==500&&snapshotCalls==0&&nextCalls==400);emit("100_healthy_taps",delivered);
 zero();delivered=0;
 for(unsigned tap=0;tap<100;++tap){
  nowMs+=10;fake_ms=nowMs;report_one(3,100,200);fail_point_reads=3;
  for(unsigned retry=0;retry<3;++retry){serviceProvider();nowMs+=5;fake_ms=nowMs;}
  serviceProvider();report_release();fail_ack_writes=1;failed_ack_reaches_controller=(tap%2)!=0;
  serviceProvider();nowMs+=5;fake_ms=nowMs;serviceProvider();
  assert(nativeTouchGetTap(p));++delivered;assert(!nativeTouchGetTap(p));
 }
 assert(polls==600&&snapshotCalls==0&&delivered==100);emit("100_taps_with_400_injected_bus_failures",delivered);
 assert(api->unsubscribe(nullptr,subscription));assert(driver->quiesce());driver->stop();assert(active_claim==0&&release_calls==1);
}
'''
(D/'probe.cpp').write_text(prefix+core+'\n}\n'+getters+bus+test)
flags=['-D_POSIX_C_SOURCE=200809L','-pthread','-fno-omit-frame-pointer','-I'+str(R/'sdk/driver'),'-I'+str(R/'test/drivers/stub_idf_i2c')]
if a.sanitize:flags+=['-fsanitize=address,undefined']
subprocess.run(['cc','-std=c11',*flags,'-c',str(R/'Drivers/gt911_touch/driver.c'),'-o',str(D/'driver.o')],check=True)
subprocess.run(['c++','-std=c++17',*flags,'-I'+str(R/'src/native'),str(D/'probe.cpp'),str(D/'driver.o'),'-o',str(D/'probe')],check=True)
subprocess.run([str(D/'probe')],env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'},check=True,timeout=20)
paths=['src/native/NativeTouchInput.cpp','Drivers/gt911_touch/driver.c','sdk/driver/RiscTouchV1.h','sdk/driver/RiscI2cBusV1.h','sdk/driver/RiscPlatformClockV1.h','test/drivers/gt911_touch_test.c','test/hal/touch_capture_behavior_test.py']
(D/'source-hashes.json').write_text(json.dumps({x:hashlib.sha256((R/x).read_bytes()).hexdigest() for x in paths},indent=2)+'\n')
