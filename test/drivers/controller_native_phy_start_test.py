#!/usr/bin/env python3
"""Execute the actual controller dependency/start body, without hardware I/O."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'Drivers/usb_controller_esp32s3/driver_base.cpp').read_text()
begin=source.index('bool start(const risc_provider_dependency_v1 *deps, size_t count) {')
end=source.index('\nstruct RolePort {',begin)
preamble=r'''
#include "RiscUsbVbusV1.h"
#include "NativePhyLease.h"
#include "StartupDiagnostic.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static StartupDiagnostic startupError;
struct { void clear(){} } enumerationDiagnostic;
struct UsbRoleSwitch { enum class State{Off,Host}; State current=State::Off;
 State state()const{return current;} void begin(uint32_t){current=State::Host;} } role;
struct {bool owns_storage()const{return false;}} ownedBulk;
static bool native_admission_guard(){return false;}
static bool running,installed,phyRouteCaptured,fault;
static void *phy,*client,*transfer;
static uint64_t powerLease;
static const risc_usb_vbus_api_v1* power;
static const risc_usb_vbus_monitor_api_v1* powerMonitor;
static RiscUsbController::NativePhyLease nativePhyLease;
static bool equals(const char*a,const char*b){return a&&b&&!strcmp(a,b);}
static bool start_failure(const char*,int){return false;}
static unsigned xTaskGetTickCount(){return 0;}
static const unsigned portTICK_PERIOD_MS=1;
static unsigned calls;
static risc_usb_phy_resource_api_v1 nativeApi={1,sizeof(nativeApi),nullptr,1,0,
 [](void*){++calls;return true;},[](void*,uint64_t*){++calls;return false;},[](void*,uint64_t){++calls;return true;}};
static risc_usb_vbus_monitor_api_v1 vbus={{1,sizeof(vbus),nullptr,
 [](void*,uint32_t,uint64_t*){++calls;return false;},[](void*,uint64_t){++calls;return true;},[](void*){++calls;return true;}},
 [](void*)->int32_t{++calls;return RISC_USB_POWER_UNKNOWN;},0};
'''
main=r'''
int main(){
 risc_provider_dependency_v1 deps[2]={{"board.power.vbus",1,&vbus},{RISC_USB_PHY_RESOURCE_CAPABILITY,1,&nativeApi}};
 auto reject=[&](const risc_provider_dependency_v1*p,size_t n){assert(!start(p,n)&&role.state()==UsbRoleSwitch::State::Off&&!calls);};
 reject(nullptr,0);reject(deps,0);reject(deps,1);reject(deps,3);
 for(unsigned i=0;i<2;++i){auto saved=deps[i];deps[i].api=nullptr;reject(deps,2);deps[i]=saved;
  deps[i].capability_id="unknown";reject(deps,2);deps[i]=saved;deps[i].api_version=2;reject(deps,2);deps[i]=saved;}
 auto second=deps[1];deps[1]=deps[0];reject(deps,2);deps[1]=second;
 const auto good=nativeApi;
 nativeApi.struct_size=8;reject(deps,2);nativeApi=good;
 nativeApi.controller_kind=0;reject(deps,2);nativeApi=good;
 nativeApi.reserved=1;reject(deps,2);nativeApi=good;
 nativeApi.is_owner=nullptr;reject(deps,2);nativeApi=good;
 nativeApi.claim=nullptr;reject(deps,2);nativeApi=good;
 nativeApi.release=nullptr;reject(deps,2);nativeApi=good;
 auto monitor=vbus.input_status;vbus.input_status=nullptr;reject(deps,2);vbus.input_status=monitor;
 assert(start(deps,2)&&!calls&&!nativePhyLease.held());
 assert(!start(deps,2)&&!calls);
 role.current=UsbRoleSwitch::State::Off;auto first=deps[0];deps[0]=deps[1];deps[1]=first;
 assert(start(deps,2)&&!calls);
 puts("Actual controller start: exact two dependencies, malformed/duplicate rejection, order-independent binding and no startup I/O PASS");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    tmp=Path(temporary);(tmp/'start.cpp').write_text(preamble+source[begin:end]+main)
    flags=['-std=c++17','-Wall','-Wextra','-Werror','-DRISC_USB_CONTROLLER_NATIVE_PHY_LEASE=1']
    if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    subprocess.run(['c++',*flags,'-I'+str(ROOT/'sdk/driver'),'-I'+str(ROOT/'Drivers/usb_controller_esp32s3'),str(tmp/'start.cpp'),'-o',str(tmp/'start')],check=True)
    subprocess.run([str(tmp/'start')],check=True)
