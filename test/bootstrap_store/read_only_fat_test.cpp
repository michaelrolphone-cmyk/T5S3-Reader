#include "platform/SdBootFatFs.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
static std::vector<uint8_t> media(4096*512);
static unsigned steps=0,limit=4096;
static void put16(size_t at,uint16_t v){media[at]=v;media[at+1]=v>>8;}
static void put32(size_t at,uint32_t v){put16(at,v);put16(at+2,v>>16);}
static void fat12(unsigned cluster,unsigned next){
 size_t at=512+cluster*3/2;uint16_t v=media[at]|uint16_t(media[at+1])<<8;
 if(cluster&1)v=(v&15)|(next<<4);else v=(v&0xf000)|(next&0xfff);
 put16(at,v);
}
extern "C" {
int risc_boot_fatfs_checkpoint(){return ++steps<=limit;}
DSTATUS disk_initialize(BYTE d){return d?STA_NOINIT:STA_PROTECT;}
DSTATUS disk_status(BYTE d){return disk_initialize(d);}
DRESULT disk_read(BYTE d,BYTE* out,LBA_t sector,UINT count){
 if(d||sector>=4096||count>4096-sector)return RES_PARERR;
 std::memcpy(out,media.data()+sector*512,count*512);return RES_OK;
}
DRESULT disk_ioctl(BYTE,BYTE,void*){return RES_PARERR;}
}
int main(){
 media[0]=0xeb;media[2]=0x90;put16(11,512);media[13]=1;put16(14,1);
 media[16]=1;put16(17,128);put16(19,4096);media[21]=0xf8;put16(22,12);
 media[510]=0x55;media[511]=0xaa;fat12(0,0xff8);fat12(1,0xfff);
 fat12(2,3);fat12(3,0xfff);fat12(4,4);
 constexpr size_t root=13*512,data=21*512;
 std::memcpy(media.data()+root,"DRIVER  ELF",11);put16(root+26,2);put32(root+28,1024);
 std::memcpy(media.data()+root+32,"LOOP       ",11);media[root+32+11]=0x10;put16(root+32+26,4);
 for(unsigned i=0;i<1024;++i)media[data+i]=uint8_t(i);
 // A directory whose FAT chain loops with no unused entries must terminate.
 for(unsigned i=0;i<512;i+=32)media[data+1024+i]=0xe5;
 auto original=media;
 FATFS fs{};assert(f_mount(&fs,"0:",1)==FR_OK);
 FIL file{};assert(f_open(&file,"0:/DRIVER.ELF",FA_READ)==FR_OK);
 std::array<uint8_t,1024> bytes{};UINT got=0;
 assert(f_read(&file,bytes.data(),bytes.size(),&got)==FR_OK&&got==1024);
 for(unsigned i=0;i<1024;++i)assert(bytes[i]==uint8_t(i));
 assert(f_close(&file)==FR_OK);
 assert(f_open(&file,"0:/NEW.ELF",FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK);
 steps=0;limit=64;
 auto result=f_open(&file,"0:/LOOP/MISSING.ELF",FA_READ);
 assert(result==FR_INT_ERR && steps<limit);
 steps=0;limit=1;
 assert(f_open(&file,"0:/LOOP/MISSING.ELF",FA_READ)==FR_DISK_ERR);
 assert(steps>limit && steps<limit+5);
 assert(media==original);
 assert(f_mount(nullptr,"0:",0)==FR_OK);
 puts("Read-only bootstrap FatFs: real file reads, no writes, cyclic directory terminates PASS");
}
