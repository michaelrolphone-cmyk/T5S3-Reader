#include "../../src/runtime/boot/HeadlessLifecycle.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace RuntimeBoot;
struct Adapter {
  int fail = -1, calls=0, held=0, mounts=0, unmounts=0, ticks=0;
  bool release=true, unmountOK=true;
  Result result() { return calls++ == fail ? Result::Fault : Result::Ready; }
  size_t count() { return 2; }
  Result mount() { ++mounts; return result(); }
  Result recover(size_t) { return result(); }
  Result inventory() { return result(); }
  Result prepare() { return result(); }
  Result bind(size_t) { auto r=result(); if(r==Result::Ready) ++held; return r; }
  void poll() { ++ticks; }
  bool shutdown() { if(!release) return false; held=0; return true; }
  bool unmount() { ++unmounts; return unmountOK; }
};
int main() {
  for (int fail=-1; fail<7; ++fail) {
    Adapter a; a.fail=fail; HeadlessLifecycle<Adapter> b(a); b.start();
    for(int i=0;i<20;++i) b.tick();
    assert(b.state()==(fail<0 ? State::Running : State::Idle));
    assert(a.held==(fail<0 ? 2 : 0));
    auto calls=a.calls;
    for(int i=0;i<20;++i) b.tick();
    assert(a.calls==calls && a.mounts==1); // no automatic retry/rehash
    b.stop(); b.tick(); assert(b.state()==State::Cold && !a.held && a.unmounts==1);
    a.fail=-1; b.start(); for(int i=0;i<20;++i) b.tick();
    assert(b.state()==State::Running && a.held==2 && a.mounts==2);
  }
  for (bool failureDuringBoot : {false,true}) {
    Adapter a; HeadlessLifecycle<Adapter> b(a);
    if(failureDuringBoot) { a.fail=6; a.release=false; }
    b.start(); for(int i=0;i<20;++i) b.tick();
    if(!failureDuringBoot) { a.release=false; b.stop(); b.tick(); }
    assert(b.state()==State::Retained && a.unmounts==0 && a.held>0);
    b.start(); b.stop(); for(int i=0;i<10;++i)b.tick();
    assert(b.state()==State::Retained && a.unmounts==0);
  }
  Adapter a; HeadlessLifecycle<Adapter> b(a); b.start();
  for(int i=0;i<20;++i)b.tick();
  a.unmountOK=false; b.stop(); b.tick();
  assert(b.state()==State::Retained && a.held==0);
  puts("Headless boot: fault rollback, retained teardown, no retry, owner progress and explicit restart PASS");
}
