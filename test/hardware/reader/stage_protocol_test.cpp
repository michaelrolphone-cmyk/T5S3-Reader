#include "StageProtocol.h"
#include <cassert>
#include <cstring>
int main(){
 struct File {size_t bytes;};
 const File files[]={{500},{300}};size_t reads=0,yields=0;unsigned elapsed=0;
 assert(ReaderStage::verifyInventory(files,[&](const auto&){reads++;return true;},[&](){return elapsed;},[&](){yields++;}));
 assert(reads==2&&yields==2);
 reads=0;assert(!ReaderStage::verifyInventory(files,[&](const auto&){reads++;return true;},[](){return 10000u;},[](){}));assert(reads==0);
 assert(!ReaderStage::verifyInventory(files,[](const auto&){return false;},[](){return 0u;},[](){}));
 elapsed=0;assert(!ReaderStage::verifyInventory(files,[&](const auto&){elapsed=10001;return true;},[&](){return elapsed;},[](){}));
 const File oversized[]={{1024u*1024u+1}};
 assert(!ReaderStage::verifyInventory(oversized,[](const auto&){assert(false);return true;},[](){return 0u;},[](){}));
 const File total[]={{1024u*1024u},{1024u*1024u},{1024u*1024u},{1024u*1024u},{1}};
 reads=0;assert(!ReaderStage::verifyInventory(total,[&](const auto&){reads++;return true;},[](){return 0u;},[](){}));assert(reads==4);
 ReaderStage::Line line;
 for(size_t i=0;i<ReaderStage::kLineBytes-1;i++)assert(line.add('x')==ReaderStage::Line::Partial);
 assert(line.add('\n')==ReaderStage::Line::Complete);assert(line.size()==ReaderStage::kLineBytes-1);line.reset();
 for(size_t i=0;i<ReaderStage::kLineBytes;i++)line.add('x');
 for(char c:std::string("RTE_STAGE_V1 forged"))assert(line.add(c)==ReaderStage::Line::Partial);
 assert(line.add('\n')==ReaderStage::Line::Dropped);
 for(char c:std::string("CMD:SCREENSHOT"))line.add(c);
 assert(line.add('\n')==ReaderStage::Line::Complete);assert(!strcmp(line.data(),"CMD:SCREENSHOT"));
 std::string p="books.elf\r\n";assert(ReaderStage::mergePins(p));assert(p=="books.elf\r\ndriver_manager.elf\napp_store.elf\n");
 const auto same=p;assert(ReaderStage::mergePins(p)&&p==same);
 p=" driver_manager.elf\rapp_store.elf\n";assert(ReaderStage::mergePins(p));assert(p==" driver_manager.elf\rapp_store.elf\ndriver_manager.elf\n");
 p=std::string("x\0y",3);assert(!ReaderStage::mergePins(p));
 p=std::string(ReaderStage::kPinsBytes,'x');assert(!ReaderStage::mergePins(p));
 p.clear();for(int i=0;i<128;i++)p+="other.elf\n";assert(!ReaderStage::mergePins(p));
}
