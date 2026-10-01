#include <cassert>
#include <cstring>
#include "runtime/boot/DefaultAppSelection.h"
FakeStore Storage;
using RuntimeDefaultApp::Selection;
int main(){
  char name[96]{};
  assert(RuntimeDefaultApp::read(name)==Selection::Absent);
  const char good[]="camera_utility.elf\n";
  Storage.present=true;Storage.data=good;Storage.size=sizeof(good)-1;
  assert(RuntimeDefaultApp::read(name)==Selection::Ready);
  assert(std::strcmp(name,"camera_utility.elf")==0);
  assert(RuntimeDefaultApp::parse("../bad.elf",10,name)==Selection::Invalid);
  assert(RuntimeDefaultApp::parse("/Apps/x.elf",11,name)==Selection::Invalid);
  const char nul[]={'x','.','e','l','f',0,'z'};
  assert(RuntimeDefaultApp::parse(nul,sizeof(nul),name)==Selection::Invalid);
  assert(RuntimeDefaultApp::parse("foo.elf\nextra",13,name)==Selection::Invalid);
  assert(RuntimeDefaultApp::parse("foo.elf\r\n",9,name)==Selection::Ready);
  Storage.shortRead=true;
  assert(RuntimeDefaultApp::read(name)==Selection::Invalid);
  Storage.shortRead=false;Storage.closeOk=false;
  assert(RuntimeDefaultApp::read(name)==Selection::Invalid);
  Storage.closeOk=true;Storage.directory=true;
  assert(RuntimeDefaultApp::read(name)==Selection::Invalid);
  Storage.directory=false;Storage.mounted=false;
  assert(RuntimeDefaultApp::read(name)==Selection::Invalid);
}
