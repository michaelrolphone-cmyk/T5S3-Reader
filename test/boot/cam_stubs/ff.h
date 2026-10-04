#pragma once
#include <cstdint>
#include <cstring>
using UINT=unsigned; using BYTE=uint8_t; using FSIZE_t=uint32_t;
enum FRESULT { FR_OK, FR_DISK_ERR, FR_INVALID_NAME, FR_INVALID_PARAMETER, FR_TIMEOUT, FR_EXIST, FR_NO_FILE, FR_NO_PATH };
constexpr BYTE AM_DIR=16,FA_READ=1,FA_WRITE=2,FA_CREATE_NEW=4,FA_CREATE_ALWAYS=8,FA_OPEN_ALWAYS=16;
struct FIL { FSIZE_t size=0,offset=0; };
struct FF_DIR {};
#ifndef FAKE_CAM_NAME_CAPACITY
#define FAKE_CAM_NAME_CAPACITY 256
#endif
struct FILINFO { uint8_t fattrib=0; char fname[FAKE_CAM_NAME_CAPACITY]{}; FSIZE_t fsize=0; };
namespace Fake {
inline bool failClose=false, failStat=false, failDirectory=false, failDirectoryClose=false;
inline unsigned io=0;
inline FILINFO entry{};
}
inline FRESULT f_stat(const char*,FILINFO*) { ++Fake::io; return Fake::failStat ? FR_DISK_ERR : FR_OK; }
inline FRESULT f_open(FIL*,const char*,BYTE) { ++Fake::io; return FR_OK; }
inline FRESULT f_opendir(FF_DIR*,const char*) { ++Fake::io; return FR_OK; }
inline FRESULT f_close(FIL*) { ++Fake::io; return Fake::failClose ? FR_DISK_ERR : FR_OK; }
inline FRESULT f_closedir(FF_DIR*) { ++Fake::io; return Fake::failDirectoryClose ? FR_DISK_ERR : FR_OK; }
inline FSIZE_t f_size(FIL* p) { return p->size; }
inline FSIZE_t f_tell(FIL* p) { return p->offset; }
inline FRESULT f_lseek(FIL* p,FSIZE_t offset) { ++Fake::io; p->offset=offset; return FR_OK; }
inline FRESULT f_read(FIL*,void*,UINT,UINT* n) { ++Fake::io; *n=0; return FR_OK; }
inline FRESULT f_write(FIL* p,const void*,UINT count,UINT* n) { ++Fake::io; *n=count; p->size+=count; return FR_OK; }
inline FRESULT f_sync(FIL*) { ++Fake::io; return FR_OK; }
inline FRESULT f_readdir(FF_DIR*,FILINFO* p) { ++Fake::io; if(Fake::failDirectory)return FR_DISK_ERR; if(p)*p=Fake::entry; return FR_OK; }
inline FRESULT f_mkdir(const char*) { ++Fake::io; return FR_OK; }
inline FRESULT f_unlink(const char*) { ++Fake::io; return FR_OK; }
inline FRESULT f_rename(const char*,const char*) { ++Fake::io; return FR_OK; }
