#pragma once
#include "PackageJsonGuard.h"
#include "PackageOrdinaryStage.h"
#include <cstdint>
#include <cstring>

namespace RuntimePackages {
// File format remains provider-abi.v1; its explicit OS/CPU revision is not the
// provider entry-point version. No unknown value is coerced or downgraded.
inline bool parseProviderAbiProfile(const char* bytes,size_t size,
 uint32_t& revision,char (&capability)[64],uint32_t& api) {
    revision=api=0;capability[0]=0;
    constexpr char prefix[]="os-cpu-abi=";
    constexpr char middle[]="\nprovides=";
    if(!bytes || size>191 || size<sizeof(prefix)+sizeof(middle)+8 ||
       std::memcmp(bytes,prefix,sizeof(prefix)-1))return false;
    size_t at=sizeof(prefix)-1;
    if(bytes[at]!='1' && bytes[at]!='2')return false;
    const uint32_t selected=bytes[at++]-'0';
    if(std::memcmp(bytes+at,middle,sizeof(middle)-1))return false;
    at+=sizeof(middle)-1;
    size_t n=0;
    while(at<size && bytes[at]!='\n') {
        if(n+1>=sizeof(capability))return false;
        capability[n++]=bytes[at++];
    }
    capability[n]=0;
    if(!safePackageCapability(capability) || size-at<7 ||
       std::memcmp(bytes+at,"\napi=",5))return false;
    at+=5;
    if(bytes[at]<'1' || bytes[at]>'9')return false;
    uint32_t number=0;
    while(at<size && bytes[at]>='0' && bytes[at]<='9') {
        unsigned digit=bytes[at++]-'0';
        if(number>(UINT32_MAX-digit)/10)return false;
        number=number*10+digit;
    }
    if(at+1!=size || bytes[at]!='\n')return false;
    revision=selected;api=number;return true;
}
inline bool providerManifestOsCpuAbi(const char* bytes,size_t size,uint32_t& revision) {
    revision=0;const char* value=nullptr;size_t length=0;bool found=false;
    if(!PackageJsonGuard(bytes,size).topLevelValue("os_cpu_abi",value,length,found))return false;
    // Historical manifests without the field mean ABI 1 only.
    if(!found){revision=1;return true;}
    if(length!=1 || (*value!='1' && *value!='2'))return false;
    revision=*value-'0';return true;
}
}
