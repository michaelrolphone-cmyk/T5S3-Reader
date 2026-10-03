#include <Arduino.h>
#include <HalStorage.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include "InstalledProof.h"
struct Expected { const char* path; size_t bytes; const char* sha; };
static const Expected expected[]={{"/Drivers/platform-clock-v1/.package.json",591,"a51f5842af04014356e93ebf0158600ef742b902cbe68b1c4f08bd550b288f00"},
{"/Drivers/platform-clock-v1/driver.elf",29544,"a5231d3f21931261b10ae82a3e16248d60a678aa6ebc67b61cb702491d6a0ece"},
{"/Drivers/platform-clock-v1/provider-abi.v1",43,"dca95c5cb8970a152329037bfff9adae0d06648cb9f93aaacff0c11c9deb6064"},
{"/Drivers/platform-clock-v1/privileged-imports.v1",21,"81b1a0e4cdc20acd7b99564c77ec5cdcaf196c41d5c6a020a6fc61c9603f8b7e"},
{"/Services/archive-zip/.package.json",631,"0faf046fb506cd372cc9f7e4350d57a91b28b58fe9820cd177660bf15b169bd6"},
{"/Services/archive-zip/driver.elf",115660,"58d02bfd19f6d49565e6502acb7e3d8e37abd64bdc8f310cd9fcd7b2e8e13676"},
{"/Services/archive-zip/provider-abi.v1",40,"6960786f57a7e2a915442b1d650ffa5bd3300f94c04e5a1532ee71055cdadebd"},
{"/Services/archive-zip/privileged-imports.v1",219,"1af85aaa12b967984071aaa8bac387fb5d80d10feb289b449fe6a3c638030bc7"}};
bool verifyInstalledProof(){
 const char* roots[]={"/Drivers/platform-clock-v1","/Services/archive-zip"};
 const char* pending[]={"/Drivers/.platform-clock-v1.pkg-stage","/Drivers/.platform-clock-v1.pkg-previous","/Drivers/.platform-clock-v1.pkg-removing","/Services/.archive-zip.pkg-stage","/Services/.archive-zip.pkg-previous","/Services/.archive-zip.pkg-removing"};
 for(auto path:pending)if(Storage.exists(path))return false;
 for(auto root:roots){auto dir=Storage.open(root);if(!dir||!dir.isDirectory())return false;unsigned count=0;for(;count<16;){auto f=dir.openNextFile();if(!f)break;char name[128]{};f.getName(name,sizeof(name));bool good=!f.isDirectory(),known=false;String full=String(root)+"/"+name;for(auto&e:expected)if(full==e.path)known=true;bool closed=f.close();if(!closed||!good||!known)return false;++count;delay(1);}bool err=dir.getError();bool closed=dir.close();if(err||!closed||count!=4)return false;}
 for(auto&e:expected){auto f=Storage.open(e.path);if(!f||f.isDirectory()||f.fileSize64()!=e.bytes)return false;mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);bool good=!mbedtls_sha256_starts_ret(&ctx,0);uint8_t buf[512],digest[32];size_t at=0;uint32_t start=millis();while(good&&at<e.bytes){if(millis()-start>=20000){good=false;break;}size_t n=std::min(sizeof(buf),e.bytes-at);good=f.read(buf,n)==int(n)&&!mbedtls_sha256_update_ret(&ctx,buf,n);at+=n;delay(1);}if(good)good=!mbedtls_sha256_finish_ret(&ctx,digest);mbedtls_sha256_free(&ctx);bool err=f.getError();bool closed=f.close();char hex[65]{};for(unsigned i=0;i<32&&good;++i)snprintf(hex+i*2,3,"%02x",digest[i]);good=good&&!err&&closed&&!strcmp(hex,e.sha);Serial.printf("DIAG_INSTALLED path=%s bytes=%u sha256=%s exact=%u\n",e.path,unsigned(at),hex,unsigned(good));if(!good)return false;}
 return true;
}
