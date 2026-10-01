#include <cassert>
#include <string>
#include <vector>
#include "WifiCredentialStore.h"
#include "HalStorage.h"

namespace JsonSettingsIO {
bool saveWifi(const WifiCredentialStore&, const char*) { return false; }
bool saveWifiSnapshot(const std::vector<WifiCredential>& credentials, const std::string& last, const char* path) {
  std::string data=last+"\n";
  for(const auto& c:credentials) data+=c.ssid+"\t"+c.password+"\n";
  return Storage.writeFile(path,String{data});
}
bool loadWifi(WifiCredentialStore& store,const char* raw,bool*) {
  const std::string data=raw?raw:"";
  if(data=="BROKEN" || data=="SCHEMA") return false;
  const auto sep=data.find('\n'); if(sep==std::string::npos)return false;
  store.lastConnectedSsid=data.substr(0,sep);
  std::vector<WifiCredential> candidate;
  size_t at=sep+1;
  while(at<data.size()){
    const auto end=data.find('\n',at);const auto line=data.substr(at,end==std::string::npos?std::string::npos:end-at);
    const auto tab=line.find('\t');if(tab==std::string::npos||line.substr(0,tab).empty())return false;
    candidate.push_back({line.substr(0,tab),line.substr(tab+1)});
    if(end==std::string::npos) break;
    at=end+1;
  }
  store.credentials=std::move(candidate);return true;
}
}

int main(){
  auto& store=WifiCredentialStore::getInstance();
  Storage.files.clear();
  assert(store.addCredential("network-a","secret-a"));
  assert(store.setLastConnectedSsid("network-a"));
  const std::string durable=Storage.files["/.crosspoint/wifi.json"];

  Storage.failWrites=1;
  assert(!store.addCredential("network-b","secret-b"));
  assert(store.getCredentials().size()==1 && store.findCredential("network-b")==nullptr);
  assert(Storage.files["/.crosspoint/wifi.json"]==durable);
  assert(store.addCredential("network-b","secret-b"));
  assert(store.getCredentials().size()==2 && store.findCredential("network-b"));

  const std::string beforeRemove=Storage.files["/.crosspoint/wifi.json"];
  Storage.failRenameToJson=1;
  assert(!store.removeCredential("network-a"));
  assert(store.findCredential("network-a") && store.findCredential("network-b"));
  assert(Storage.files["/.crosspoint/wifi.json"]==beforeRemove);
  assert(store.removeCredential("network-a"));
  assert(!store.findCredential("network-a") && store.findCredential("network-b"));

  const std::string priorLast=store.getLastConnectedSsid();
  Storage.failWrites=1;
  assert(!store.setLastConnectedSsid("network-b"));
  assert(store.getLastConnectedSsid()==priorLast);
  Storage.failWrites=1;
  assert(!store.clearAll() && store.getCredentials().size()==1);
  assert(store.clearAll() && store.getCredentials().empty());

  // A valid load followed by a corrupt primary without a recoverable snapshot
  // must fail closed instead of exposing the old in-memory credential set.
  assert(store.addCredential("cached-only","synthetic"));
  Storage.files["/.crosspoint/wifi.json"]="BROKEN";
  Storage.remove("/.crosspoint/wifi.json.bak");
  assert(!store.loadFromFile());
  assert(store.getCredentials().empty() && store.getLastConnectedSsid().empty());

  // Interrupted promotion leaves the prior complete snapshot recoverable.
  Storage.files["/.crosspoint/wifi.json.bak"]=durable;
  assert(store.loadFromFile());
  assert(store.findCredential("network-a") && store.getLastConnectedSsid()=="network-a");
}
