#include "ff.h"
#include "diskio.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
static std::vector<uint8_t> card(131072u*512u);
extern "C" {
int risc_fatfs_checkpoint(){return 1;}
DSTATUS disk_initialize(BYTE d){return d?STA_NOINIT:0;}
DSTATUS disk_status(BYTE d){return disk_initialize(d);}
DRESULT disk_read(BYTE d,BYTE* out,LBA_t s,UINT n){assert(d==0&&s+n<=131072);memcpy(out,card.data()+size_t(s)*512,n*512);return RES_OK;}
DRESULT disk_write(BYTE d,const BYTE* in,LBA_t s,UINT n){assert(d==0&&s+n<=131072);memcpy(card.data()+size_t(s)*512,in,n*512);return RES_OK;}
DRESULT disk_ioctl(BYTE,BYTE cmd,void*){return cmd==CTRL_SYNC?RES_OK:RES_PARERR;}
}
static void p16(size_t at,uint16_t v){card[at]=v;card[at+1]=v>>8;}
static void p32(size_t at,uint32_t v){p16(at,v);p16(at+2,v>>16);}
int main(int argc,char**argv){assert(argc==3);card[0]=0xeb;card[1]=0x58;card[2]=0x90;memcpy(card.data()+3,"MSDOS5.0",8);p16(11,512);card[13]=1;p16(14,32);card[16]=2;card[21]=0xf8;p32(32,131072);p32(36,1024);p32(44,2);p16(48,1);card[66]=0x29;memcpy(card.data()+82,"FAT32   ",8);card[510]=0x55;card[511]=0xaa;for(unsigned f=0;f<2;f++){auto b=(32+f*1024)*512;p32(b,0xffffff8);p32(b+4,0xffffffff);p32(b+8,0xfffffff);}
FATFS fs{};assert(f_mount(&fs,"0:",1)==FR_OK);auto src=std::filesystem::path(argv[1]);
std::vector<std::filesystem::directory_entry> entries;for(auto const& entry:std::filesystem::recursive_directory_iterator(src))entries.push_back(entry);std::sort(entries.begin(),entries.end(),[](auto const&a,auto const&b){return a.path().generic_string()<b.path().generic_string();});for(auto const& entry:entries) {auto rel=std::filesystem::relative(entry.path(),src).generic_string();std::string path="0:/"+rel;if(entry.is_directory()){assert(f_mkdir(path.c_str())==FR_OK);}else if(entry.is_regular_file()){std::ifstream f(entry.path(),std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});FIL out{};assert(f_open(&out,path.c_str(),FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);UINT got=0;assert(f_write(&out,bytes.data(),bytes.size(),&got)==FR_OK&&got==bytes.size());assert(f_close(&out)==FR_OK);}}
assert(f_mount(nullptr,"0:",0)==FR_OK);std::ofstream out(argv[2],std::ios::binary);out.write((char*)card.data(),card.size());assert(out);}
