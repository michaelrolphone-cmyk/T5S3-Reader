#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include "NativePhyLease.h"
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owner=true,idle=true,suspendOk=true,resumeOk=true,partial=false;
static unsigned suspends,resumes,io;
static Hardware hardware() {
    Hardware h{};h.owner=[](){return owner;};h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){++io;};
    h.gpioOpen=[](uint8_t,bool,bool,bool){++io;return true;};h.gpioWrite=[](uint8_t,bool){++io;return true;};
    h.gpioRead=[](uint8_t,bool*){++io;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++io;return true;};h.gpioClose=[](uint8_t){++io;return true;};
    h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++io;return true;};h.i2cClose=[](uint8_t){++io;return true;};
    h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++io;return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++io;return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++io;return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++io;return true;};h.spiClose=[](uint8_t){++io;return true;};
    h.usbPhyIdle=[](){return idle;};
    h.usbPhySuspend=[](){++suspends;if(suspendOk||partial)idle=false;return suspendOk;};
    h.usbPhyResume=[](){++resumes;if(resumeOk)idle=true;return resumeOk;};return h;
}
int main() {
    RiscBoot::Runtime runtime({[](){return owner;},nullptr,nullptr,nullptr});
    Port port(hardware());assert(port.bind(runtime));
    auto &resource=port.usb_;auto &api=resource.api;
    RiscUsbController::NativePhyLease lease, competitor;
    assert(!lease.claim() && lease.release());
    assert(lease.bind(&api) && !suspends && !resumes && !io);
    auto rejected=[&](risc_usb_phy_resource_api_v1 bad){assert(!lease.bind(&bad) && !suspends && !resumes);};
    auto bad=api;bad.api_version=2;rejected(bad);bad=api;bad.struct_size=8;rejected(bad);
    bad=api;bad.controller_kind=0;rejected(bad);bad=api;bad.reserved=1;rejected(bad);
    bad=api;bad.is_owner=nullptr;rejected(bad);bad=api;bad.claim=nullptr;rejected(bad);bad=api;bad.release=nullptr;rejected(bad);
    assert(!lease.bind(nullptr));
    owner=false;assert(!lease.claim() && !lease.held() && !suspends);owner=true;
    port.pins_[19].owner=&port;assert(!lease.claim() && !lease.held() && !suspends);port.pins_[19]={};
    suspendOk=false;assert(!lease.claim() && !lease.held() && idle && suspends==1);
    assert(lease.release() && !resumes);
    partial=true;assert(!lease.claim() && lease.held() && !idle && resource.closing);
    const auto token=resource.token;
    assert(!lease.bind(&api) && !lease.unbind() && !lease.claim() && resource.token==token);
    assert(!port.appExitSafe() && !port.restartResourcesSafe() && !port.quiescent());
    assert(port.providerStorageSafe());
    resumeOk=false;assert(!lease.release() && lease.held() && resource.token==token);
    auto calls=resumes;owner=false;assert(!lease.release() && resumes==calls);owner=true;
    resumeOk=true;assert(lease.release() && !lease.held() && !resource.token && idle);
    assert(!port.pins_[19].owner && !port.pins_[20].owner && port.appExitSafe());
    assert(lease.release() && resumes==calls+1);
    suspendOk=true;partial=false;assert(lease.claim() && lease.held() && resource.token>token);
    assert(competitor.bind(&api) && !competitor.claim() && !competitor.held());
    assert(competitor.release() && lease.held());
    assert(!port.appExitSafe() && !port.restartResourcesSafe());
    assert(lease.release() && lease.unbind() && competitor.unbind() && !lease.claim());
    assert(port.appExitSafe() && port.restartResourcesSafe() && !io);
    puts("Controller lease against actual Runtime Port/table: admission, owner/pad exclusion, partial claim, exact-token retained release and lifecycle fences PASS");
}
