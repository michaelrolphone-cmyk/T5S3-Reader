#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <fcntl.h>
inline std::map<std::string,std::vector<uint8_t>> files;
inline bool validPayload=true,closeGood=true,handoffGood=true,installGood=true;
inline unsigned installs=0,verifications=0,handoffs=0,now=0,delayStep=1;
inline uint32_t millis(){return now;}
inline void delay(unsigned){now+=delayStep;}
#define LOG_INF(...) ((void)0)
#define ESP_OK 0
inline int native_app_register_sd_vfs(){return 0;}
struct File {
 std::string path;size_t pos=0;bool valid=false;
 explicit operator bool()const{return valid;}
 size_t fileSize64()const{return files.at(path).size();}
 int read(void* dst,size_t n){if(!valid)return -1;auto&v=files[path];n=std::min(n,v.size()-pos);memcpy(dst,v.data()+pos,n);pos+=n;return int(n);}
 size_t write(const void* src,size_t n){auto&v=files[path];auto*b=static_cast<const uint8_t*>(src);v.insert(v.end(),b,b+n);return n;}
 bool close(){bool result=valid&&closeGood;valid=false;return result;}
};
struct StorageType {
 bool begin(){return true;}bool mkdir(const char*,bool){return true;}
 bool exists(const char*p){return files.count(p)!=0;}
 File open(const char*p,int flags){if(flags&O_CREAT){if(exists(p)&&(flags&O_EXCL))return {};files[p]={};}return {p,0,exists(p)};}
};
inline StorageType Storage;
namespace BootstrapHalStorage {inline bool releaseForHandoff(){++handoffs;return handoffGood;}}
namespace RuntimePackages {
enum class Kind{Driver};
struct Identity{char version[32]{};bool legacyVersion=false;};
struct PackageRuntimePolicy{const char* arch;unsigned abi;unsigned maxMemory;unsigned maxArchive;};
struct OrdinaryTransactionPaths{char backup[128],removing[128];};
inline bool ordinaryTransactionPaths(Kind,const char*id,OrdinaryTransactionPaths&p){snprintf(p.backup,sizeof(p.backup),"/Drivers/.%s.pkg-backup",id);snprintf(p.removing,sizeof(p.removing),"/Drivers/.%s.pkg-removing",id);return true;}
inline uint32_t installedCapabilityVersion(const char*){return 1;}
inline bool makeIdentity(Kind,const char*,const char*version,const char*,bool,Identity*out){strcpy(out->version,version);return true;}
inline bool verifyManagedOrdinarySdDirectory(const char*,Kind,const char* id,const PackageRuntimePolicy&,uint32_t(*)(const char*),Identity& observed){++verifications;strcpy(observed.version,!strcmp(id,"cam-ov3660-profile")?"0.1.0":"0.1.9");return validPayload;}
enum class OrdinaryInstallResult{Installed,PublicationRejected};
struct Outcome{OrdinaryInstallResult result;};
inline Outcome installOrdinaryFromSdZip(const char*,const PackageRuntimePolicy&,uint32_t(*)(const char*),const Identity*){++installs;return {installGood?OrdinaryInstallResult::Installed:OrdinaryInstallResult::PublicationRejected};}
}
