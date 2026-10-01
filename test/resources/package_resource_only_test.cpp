#include "runtime/packages/PackageOrdinaryManifest.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
using namespace RuntimePackages;
static std::string replace(std::string value,const std::string& a,const std::string& b){
 const auto p=value.find(a);assert(p!=std::string::npos);value.replace(p,a.size(),b);return value;
}
int main(){
 auto plan=std::make_unique<OrdinaryPackagePlan>();
 const std::string json="{\"schema\":3,\"kind\":\"service\",\"id\":\"reference-pack\",\"version\":\"1.0.0\","
 "\"payload\":\"resources\",\"artifact\":null,\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,"
 "\"entries\":[{\"name\":\"help/guide.txt\",\"size_bytes\":12,\"sha256\":\""+std::string(64,'a')+
 "\",\"executable\":false}],\"requires\":[],\"resource_imports\":[]}";
 auto parse=[&](const std::string& input){return parseOrdinaryManifest(input.data(),input.size(),*plan);};
 assert(parse(json)&&resourceOnly(plan->identity)&&!plan->identity.artifact[0]);
 const Identity identity=plan->identity;
 Identity copy{};assert(canonicalIdentity(identity,&copy)&&resourceOnly(copy));
 assert(!makeIdentity(Kind::Service,"reference-pack","1.0.0","",false,&copy));
 assert(decidePackageVersion(identity,nullptr)==InstallDecision::FreshInstall);
 assert(!parse(replace(json,"\"schema\":3","\"schema\":2")));
 assert(!parse(replace(json,"\"resources\"","\"executable\"")));
 assert(!parse(replace(json,"\"service\"","\"driver\"")));
 assert(!parse(replace(json,"\"service\"","\"application\"")));
 assert(!parse(replace(json,"\"artifact\":null","\"artifact\":\"missing.elf\"")));
 assert(!parse(replace(json,"\"executable\":false","\"executable\":true")));
 assert(!parse(replace(json,"help/guide.txt","concealed.elf")));
 assert(!parse(replace(json,"\"requires\":[]","\"requires\":[{\"capability\":\"platform.clock\",\"min_api\":1}]")));
 assert(!parse(replace(json,"\"resource_imports\":[]","\"resource_imports\":[{\"id\":\"other\",\"min_version\":\"1.0.0\"}]")));
 auto app=replace(json,"\"service\"","\"application\"");
 app=replace(app,"\"resources\"","\"executable\"");
 app=replace(app,"\"artifact\":null","\"artifact\":\"reader.elf\"");
 app=replace(app,"help/guide.txt","reader.elf");app=replace(app,"size_bytes\":12","size_bytes\":64");
 app=replace(app,"executable\":false","executable\":true");
 app=replace(app,"\"resource_imports\":[]","\"resource_imports\":[{\"id\":\"reference-pack\",\"min_version\":\"1.0.0\"}]");
 assert(parse(app)&&!resourceOnly(plan->identity)&&plan->resourceImportCount==1);
 assert(!parse(replace(app,"\"min_version\":\"1.0.0\"","\"min_version\":\"01.0.0\"")));
 assert(!parse(replace(app,"\"id\":\"reference-pack\",\"min_version\"","\"id\":\"../outside\",\"min_version\"")));
 std::cout<<"Explicit resource-only service identity and executable resource-import requests PASS\n";
}
