#ifndef RISCRTE_CI_SERIAL_STAGING
#error "SerialStaging is a CI-only utility, never a production source"
#endif
#include "SerialStaging.h"
#include "StageProtocol.h"
#include "StageIdentity.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include "runtime/packages/PackageJsonGuard.h"
#include "runtime/packages/PackageMutationGate.h"
#include "runtime/packages/PackageOrdinarySdZipAdapter.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace {
namespace P=RuntimePackages;
constexpr P::PackageRuntimePolicy policy{"xtensa-esp32s3",2,8u*1024u*1024u,16u*1024u*1024u};
constexpr char prefix[]="RTE_STAGE_V1 ";
constexpr size_t lineMax=3072,chunkMax=512,pinsMax=16384;
constexpr char pinJournal[]="/Apps/.home_apps.ci-stage-journal";
ReaderStage::Line input;bool active=false;
std::string session,snapshot,pinHash;bool pinExists=false;
uint32_t sequence=0,began=0,last=0;size_t received=0;
struct Upload {bool begun=false,installed=false;size_t offset=0;std::string part,zip;} uploads[2];
struct Info {size_t bytes=0;std::string hash;};

bool hex(const char* p,size_t n){if(!p||strlen(p)!=n)return false;for(size_t i=0;i<n;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return false;return true;}
bool fields(JsonObjectConst o,std::initializer_list<const char*> keys){if(o.size()!=keys.size())return false;for(auto k:keys)if(!o[k].is<JsonVariantConst>()||o[k].isNull())return false;return true;}
std::string digest(const uint8_t* p,size_t n){uint8_t h[32];if(mbedtls_sha256_ret(p,n,h,0))return {};char out[65];for(int i=0;i<32;i++)snprintf(out+i*2,3,"%02x",h[i]);return out;}
std::string token(){uint8_t bytes[16];esp_fill_random(bytes,sizeof(bytes));char out[33];for(int i=0;i<16;i++)snprintf(out+i*2,3,"%02x",bytes[i]);return out;}
int idIndex(const char* id){if(!id)return -1;for(int i=0;i<2;i++)if(!strcmp(id,stageApps[i].id))return i;return -1;}
std::string root(int i){return std::string("/Apps/")+stageApps[i].id;}
bool expired(){return active&&(millis()-began>180000||millis()-last>30000);}
void desc(JsonObject o,const Info& info){o["bytes"]=info.bytes;o["sha256"]=info.hash;}

bool fileInfo(const std::string& path,size_t limit,Info& info,std::string* bytes=nullptr,uint32_t budgetStart=0,bool sharedBudget=false){
 if(sharedBudget&&millis()-budgetStart>=10000)return false;
 auto f=Storage.open(path.c_str(),O_RDONLY);if(!f||f.isDirectory()){if(f)(void)f.close();return false;}
 const uint64_t size=f.fileSize64();if(size>limit){(void)f.close();return false;}
 mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);bool ok=!mbedtls_sha256_starts_ret(&ctx,0);
 const uint32_t start=sharedBudget?budgetStart:millis();uint8_t block[512],hash[32];size_t at=0;if(bytes)bytes->clear();
 while(ok&&at<size){size_t n=std::min<size_t>(sizeof(block),size-at);ok=millis()-start<10000&&f.read(block,n)==int(n);
  if(ok){ok=!mbedtls_sha256_update_ret(&ctx,block,n);if(bytes)bytes->append(reinterpret_cast<char*>(block),n);at+=n;}vTaskDelay(1);}
 ok=f.close()&&ok&&millis()-start<10000;if(ok)ok=!mbedtls_sha256_finish_ret(&ctx,hash);mbedtls_sha256_free(&ctx);if(!ok)return false;
 char text[65];for(int i=0;i<32;i++)snprintf(text+i*2,3,"%02x",hash[i]);info={size_t(size),text};return true;
}
bool sdInventoryMatches(){
 // Read through the already initialized provider VFS only. No mount, fallback,
 // raw controller, driver mutation or ELF payload is embedded in this utility.
 const uint32_t start=millis();
 return ReaderStage::verifyInventory(stageSdFiles,
   [&](const auto& expected){Info observed;return fileInfo(expected.name,expected.bytes,observed,nullptr,start,true)&&
      observed.bytes==expected.bytes&&observed.hash==expected.sha;},
   [&](){return millis()-start;},[](){vTaskDelay(1);});
}
bool pinsInfo(Info& info,std::string* bytes=nullptr){if(!Storage.exists("/Apps/.home_apps")){info={0,digest(nullptr,0)};if(bytes)bytes->clear();return true;}return fileInfo("/Apps/.home_apps",pinsMax,info,bytes);}
bool noMarkers(int i){P::OrdinaryTransactionPaths p{};return P::ordinaryTransactionPaths(P::Kind::Application,stageApps[i].id,p)&&!Storage.exists(p.stage)&&!Storage.exists(p.backup)&&!Storage.exists(p.removing);}
bool expectedFile(int i,int j,const Info& f){return f.bytes==stageApps[i].files[j].bytes&&f.hash==stageApps[i].files[j].sha;}
const char* appInventory(int i,JsonObject files){
 if(!noMarkers(i))return "conflict";
 if(Storage.exists((std::string("/Apps/")+stageApps[i].id+".elf").c_str())||Storage.exists((std::string("/Apps/")+stageApps[i].id+".json").c_str()))return "conflict";
 const std::string path=root(i);if(!Storage.exists(path.c_str()))return "absent";
 auto dir=Storage.open(path.c_str(),O_RDONLY);if(!dir||!dir.isDirectory()){if(dir)(void)dir.close();return "conflict";}
 bool good=true;size_t entries=0;unsigned seen=0;const uint32_t start=millis();
 while(entries<16&&millis()-start<10000){auto f=dir.openNextFile();if(!f)break;entries++;char name[192]{};
  const bool named=f.getName(name,sizeof(name))>0&&!f.isDirectory();good=f.close()&&good;
  int match=-1;if(named)for(int j=0;j<3;j++)if(!strcmp(name,stageApps[i].files[j].name))match=j;
  if(match<0||(seen&(1u<<match))){good=false;continue;}seen|=1u<<match;
  Info info;if(!fileInfo(path+"/"+name,65536,info)){good=false;continue;}desc(files[name].to<JsonObject>(),info);good=expectedFile(i,match,info)&&good;vTaskDelay(1);
 }
 good=good&&entries==3&&seen==7&&!dir.getError();good=dir.close()&&good;
 if(good){P::Identity observed{};good=P::verifyManagedOrdinarySdDirectory(path.c_str(),P::Kind::Application,stageApps[i].id,policy,P::installedCapabilityVersion,observed)&&!strcmp(observed.version,"1.0.8");}
 return good?"matching":"conflict";
}
bool absent(int i){return noMarkers(i)&&!Storage.exists(root(i).c_str())&&!Storage.exists((root(i)+".elf").c_str())&&!Storage.exists((root(i)+".json").c_str());}
bool stillPins(){Info info;return pinsInfo(info)&&Storage.exists("/Apps/.home_apps")==pinExists&&info.hash==pinHash;}
bool snapshotMatches(JsonObjectConst args){const char* p=args["snapshot"];return p&&!snapshot.empty()&&snapshot==p;}
bool writeFresh(const std::string& path,const std::string& data){
 if(Storage.exists(path.c_str()))return false;auto f=Storage.open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL);if(!f)return false;
 bool ok=true;const uint32_t start=millis();for(size_t at=0;ok&&at<data.size();){size_t n=std::min<size_t>(512,data.size()-at);ok=millis()-start<10000&&f.write(data.data()+at,n)==n;at+=n;vTaskDelay(1);}ok=f.close()&&ok;
 Info info;return ok&&fileInfo(path,pinsMax,info)&&info.bytes==data.size()&&info.hash==digest(reinterpret_cast<const uint8_t*>(data.data()),data.size());
}

bool operation(const char* op,JsonObjectConst a,JsonObject out){
 if(!strcmp(op,"inventory")){
  if(a.size()||Storage.exists(pinJournal))return false;for(auto& u:uploads)if(u.begun&&!u.installed)return false;
  Info info;if(!pinsInfo(info))return false;pinExists=Storage.exists("/Apps/.home_apps");pinHash=info.hash;snapshot=token();
  out["snapshot"]=snapshot;out["pins_exists"]=pinExists;desc(out["pins"].to<JsonObject>(),info);
  for(int i=0;i<2;i++){auto item=out["apps"][stageApps[i].id].to<JsonObject>();item["state"]=appInventory(i,item["files"].to<JsonObject>());}return true;
 }
 if(!strcmp(op,"read")){
  if(!fields(a,{"snapshot","selector","offset","count"})||!snapshotMatches(a)||!a["offset"].is<uint32_t>()||!a["count"].is<uint32_t>())return false;
  const char* sel=a["selector"];if(!sel)return false;std::string path;
  if(!strcmp(sel,"pins"))path="/Apps/.home_apps";
  for(int i=0;i<2;i++)for(int j=0;j<3;j++)if(std::string(stageApps[i].id)+"/"+stageApps[i].files[j].name==sel)path=root(i)+"/"+stageApps[i].files[j].name;
  const size_t offset=a["offset"],count=a["count"];if(path.empty()||!count||count>chunkMax)return false;
  auto f=Storage.open(path.c_str(),O_RDONLY);if(!f||f.isDirectory()){if(f)(void)f.close();return false;}
  const auto n=f.fileSize64();uint8_t data[512];bool ok=offset<=n&&count<=n-offset&&f.seek64(offset)&&f.read(data,count)==int(count);ok=f.close()&&ok;if(!ok)return false;
  unsigned char encoded[685]{};size_t length=0;if(mbedtls_base64_encode(encoded,sizeof(encoded),&length,data,count))return false;out["data"]=reinterpret_cast<char*>(encoded);return true;
 }
 if(!strcmp(op,"begin")){
  if(!fields(a,{"snapshot","id","bytes","sha256"})||!snapshotMatches(a)||!stillPins())return false;
  int i=idIndex(a["id"]);if(i<0||uploads[i].begun||!a["bytes"].is<uint32_t>()||a["bytes"].as<size_t>()!=stageApps[i].bytes||strcmp(a["sha256"]|"",stageApps[i].sha)||!absent(i))return false;
  P::ScopedPackageMutation gate;if(!gate)return false;
  const std::string dir="/Packages/Inbox/ci-stage-"+session;
  if(!Storage.exists(dir.c_str())&&!Storage.mkdir(dir.c_str(),true))return false;
  auto& u=uploads[i];u.part=dir+"/"+stageApps[i].id+".part";u.zip=dir+"/"+stageApps[i].id+".rte.zip";
  if(Storage.exists(u.part.c_str())||Storage.exists(u.zip.c_str()))return false;
  auto f=Storage.open(u.part.c_str(),O_WRONLY|O_CREAT|O_EXCL);if(!f||!f.close())return false;u.begun=true;out["offset"]=0;return true;
 }
 if(!strcmp(op,"chunk")){
  if(!fields(a,{"id","offset","data","sha256"})||!a["offset"].is<uint32_t>())return false;
  int i=idIndex(a["id"]);if(i<0)return false;auto& u=uploads[i];const char* data=a["data"],*hash=a["sha256"];
  if(!u.begun||u.installed||!data||strlen(data)>684||!hex(hash,64)||a["offset"].as<size_t>()!=u.offset)return false;
  uint8_t block[512];size_t n=0;if(mbedtls_base64_decode(block,sizeof(block),&n,reinterpret_cast<const unsigned char*>(data),strlen(data))||!n||n>stageApps[i].bytes-u.offset||digest(block,n)!=hash)return false;
  auto f=Storage.open(u.part.c_str(),O_RDWR);if(!f)return false;bool ok=f.fileSize64()==u.offset&&f.seek64(u.offset)&&f.write(block,n)==n;ok=f.close()&&ok;if(!ok)return false;
  u.offset+=n;out["offset"]=u.offset;return true;
 }
 if(!strcmp(op,"install")){
  if(!fields(a,{"id","snapshot"})||!snapshotMatches(a))return false;int i=idIndex(a["id"]);if(i<0)return false;
  auto& u=uploads[i];if(!u.begun||u.installed||u.offset!=stageApps[i].bytes)return false;
  P::ScopedPackageMutation gate;if(!gate){out["error"]="mutation_busy";return false;}
  if(!absent(i)){out["error"]="destination_changed";return false;}
  Info info;if(!fileInfo(u.part,65536,info)||info.bytes!=stageApps[i].bytes||info.hash!=stageApps[i].sha||Storage.exists(u.zip.c_str())||!Storage.rename(u.part.c_str(),u.zip.c_str()))return false;
  P::Identity expected{};strcpy(expected.id,stageApps[i].id);strcpy(expected.version,"1.0.8");strcpy(expected.artifact,stageApps[i].files[1].name);
  const auto result=P::installOrdinaryFromSdZip(u.zip.c_str(),policy,P::installedCapabilityVersion,&expected);
  out["installer_result"]=static_cast<unsigned>(result.result);
  if(result.result!=P::OrdinaryInstallResult::Installed)return false;
  JsonDocument check;if(strcmp(appInventory(i,check.to<JsonObject>()),"matching"))return false;
  u.installed=true;out.remove("installer_result");out["state"]="installed";out["archive_sha256"]=stageApps[i].sha;return true;
 }
 if(!strcmp(op,"pins")){
  if(!fields(a,{"snapshot","before_sha256","before_exists","after_sha256"})||!snapshotMatches(a)||!a["before_exists"].is<bool>()||a["before_exists"].as<bool>()!=pinExists||strcmp(a["before_sha256"]|"",pinHash.c_str()))return false;
  P::ScopedPackageMutation gate;if(!gate||!stillPins())return false;
  for(int i=0;i<2;i++){JsonDocument check;if(strcmp(appInventory(i,check.to<JsonObject>()),"matching"))return false;}
  Info original;std::string pins;if(!pinsInfo(original,&pins)||!ReaderStage::mergePins(pins))return false;
  const auto hash=digest(reinterpret_cast<const uint8_t*>(pins.data()),pins.size());if(strcmp(a["after_sha256"]|"",hash.c_str()))return false;
  if(hash!=original.hash||!pinExists){
   const std::string part="/Apps/.home_apps.ci-"+session+".part",backup="/Apps/.home_apps.ci-"+session+".previous";
   if(Storage.exists(backup.c_str())||Storage.exists(pinJournal)||!writeFresh(part,pins)||!stillPins())return false;
   // A durable fixed-path journal prevents a later session treating the brief
   // rename gap as an originally absent pin file. Ambiguous recovery is never
   // guessed; retain prior pins and refuse a new inventory until reconciled.
   const std::string journal=session+"\n"+original.hash+"\n"+hash+"\n"+(pinExists?"present\n":"absent\n");
   if(!writeFresh(pinJournal,journal))return false;
   if(pinExists&&!Storage.rename("/Apps/.home_apps",backup.c_str()))return false;
   if(!Storage.rename(part.c_str(),"/Apps/.home_apps")){if(pinExists&&!Storage.exists("/Apps/.home_apps"))(void)Storage.rename(backup.c_str(),"/Apps/.home_apps");out["error"]="pin_publication_incomplete";return false;}
  }
  Info verify;if(!pinsInfo(verify)||verify.hash!=hash||verify.bytes!=pins.size())return false;
  if(Storage.exists(pinJournal)&&!Storage.remove(pinJournal))return false;
  desc(out,verify);return true;
 }
 return false;
}

void dispatch(char* bytes,size_t n){
 if(n<=sizeof(prefix)-1||memcmp(bytes,prefix,sizeof(prefix)-1))return;
 const char* json=bytes+sizeof(prefix)-1;const size_t length=n-(sizeof(prefix)-1);
 P::PackageJsonGuard guard(json,length);if(!guard.objectOnly()){active=false;return;}
 JsonDocument request,reply;if(deserializeJson(request,json,length))return;
 auto q=request.as<JsonObjectConst>();if(!fields(q,{"session","sequence","op","args"})||!q["sequence"].is<uint32_t>()||!q["args"].is<JsonObjectConst>())return;
 const char* sid=q["session"],*op=q["op"];const uint32_t seq=q["sequence"];if(!hex(sid,32)||!op)return;
 auto args=q["args"].as<JsonObjectConst>();auto result=reply["result"].to<JsonObject>();bool ok=false;
 if(!strcmp(op,"hello")&&!active&&seq==1&&Storage.ready()&&fields(args,{"source_sha","sd_archive_sha256","utility_id"})&&
   !strcmp(args["source_sha"]|"",stageBase)&&!strcmp(args["sd_archive_sha256"]|"",stageSdArchive)&&!strcmp(args["utility_id"]|"",stageUtility)&&sdInventoryMatches()){
  active=true;session=sid;sequence=1;began=last=millis();received=0;snapshot.clear();for(auto& u:uploads)u=Upload{};
  result["source_sha"]=stageBase;result["sd_archive_sha256"]=stageSdArchive;result["utility_id"]=stageUtility;result["protocol"]=1;result["sd_verified_files"]=sizeof(stageSdFiles)/sizeof(stageSdFiles[0]);
  auto scope=result["scope"].to<JsonArray>();for(auto& app:stageApps)scope.add(app.id);ok=true;
 }else if(active&&session==sid&&seq==sequence+1&&seq<=512&&!expired()&&Storage.ready()){
  sequence=seq;last=millis();ok=operation(op,args,result);
 }
 reply["session"]=sid;reply["sequence"]=seq;reply["ok"]=ok;if(!ok){if(result.size()==0)result["error"]="refused";active=false;}
 std::string encoded(prefix);serializeJson(reply,encoded);encoded+='\n';if(encoded.size()<=lineMax)logSerial.write(reinterpret_cast<const uint8_t*>(encoded.data()),encoded.size());else active=false;
 vTaskDelay(1);
}
}

bool readerStageActive(){return active&&!expired();}
bool readerStageTick(){
 if(expired())active=false;
 const uint32_t start=millis();size_t count=0;bool screenshot=false;
 while(logSerial.available()>0&&count<256&&millis()-start<4){int c=logSerial.read();if(c<0)break;count++;
  if(active&&++received>2u*1024u*1024u)active=false;
  const auto state=input.add(char(c));
  if(state==ReaderStage::Line::Dropped)active=false;
  if(state==ReaderStage::Line::Complete){
   if(!active&&!strcmp(input.data(),"CMD:SCREENSHOT"))screenshot=true;else dispatch(input.data(),input.size());input.reset();
  }
 }
 return screenshot;
}
