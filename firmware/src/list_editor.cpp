#include "list_editor.h"
#include "mp24_board.h"
#include "radio_service.h"
#include "attack_service.h"
#include "lab_tools.h"
#include "network_service.h"
#include "capture_format.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_random.h>
namespace hackerphone {
char editorMessage[80]="Ready",editorPath[160]{};
static char names[64][33]{};static unsigned count=0;
unsigned ssidCount(){return count;}
const char* ssidName(unsigned i){return i<count?names[i]:"";}
static bool validName(const char* s){if(!s||!*s||strlen(s)>32)return false;for(const char* p=s;*p;++p)if(uint8_t(*p)<32||uint8_t(*p)>126)return false;return true;}
bool ssidAdd(const char* s){if(count>=64||!validName(s))return false;for(unsigned i=0;i<count;++i)if(!strcmp(s,names[i]))return false;snprintf(names[count++],33,"%s",s);return true;}
bool ssidRemove(unsigned i){if(i>=count)return false;for(unsigned j=i+1;j<count;++j)memcpy(names[j-1],names[j],33);--count;return true;}
void ssidClear(){count=0;}
bool ssidGenerate(unsigned n){if(!n||n>64-count)return false;unsigned original=count;for(unsigned tries=0;count<original+n&&tries<n*4;++tries){char s[33];snprintf(s,sizeof(s),"HP-%08lx",(unsigned long)esp_random());ssidAdd(s);}return count==original+n;}
static bool validPath(const char* p){return mp24::cardReady&&p&&!strncmp(p,"/hackerphone/",13)&&strlen(p)<160&&safeRelativePath(p+13);}
static bool newPath(const char* kind,const char* ext){if(!mp24::cardReady)return false;SD.mkdir("/hackerphone");SD.mkdir("/hackerphone/lists");for(unsigned n=1;n<100000;++n){snprintf(editorPath,sizeof(editorPath),"/hackerphone/lists/%s-%05u.%s",kind,n,ext);if(!SD.exists(editorPath))return true;}return false;}
bool ssidLoad(const char* p){
 if(!validPath(p))return false;File f=SD.open(p);if(!f||f.isDirectory()||f.size()>4096){f.close();return false;}
 auto* staged=static_cast<char(*)[33]>(ps_calloc(64,33));if(!staged){f.close();return false;}unsigned used=0;bool ok=true;
 while(f.available()){char line[35];size_t n=f.readBytesUntil('\n',line,34);if(n&&line[n-1]=='\r')--n;line[n]=0;if(!n)continue;
   if(n>32||!validName(line)){ok=false;break;}bool duplicate=false;for(unsigned i=0;i<used;++i)duplicate|=!strcmp(staged[i],line);if(duplicate)continue;if(used==64){ok=false;break;}memcpy(staged[used++],line,n+1);}
 f.close();if(ok){count=used;memcpy(names,staged,sizeof(names));snprintf(editorPath,sizeof(editorPath),"%s",p);}free(staged);return ok;
}
bool ssidSave(){if(!newPath("playlist","txt"))return false;File f=SD.open(editorPath,FILE_WRITE);if(!f)return false;bool ok=true;for(unsigned i=0;i<count;++i)if(f.println(names[i])!=strlen(names[i])+2){ok=false;break;}f.flush();f.close();if(ok)snprintf(attackOptions.ssidPath,sizeof(attackOptions.ssidPath),"%s",editorPath);return ok;}
static int slot(const uint8_t mac[6]){int empty=-1;for(unsigned i=0;i<ReconCore::Capacity;++i){auto& t=recon.targets[i];if(t.used&&!t.ble&&!memcmp(t.mac,mac,6))return i;if(!t.used&&empty<0)empty=i;}return empty;}
static void manual(Target& t,const uint8_t mac[6]){t={};t.used=true;memcpy(t.mac,mac,6);t.first=t.last=millis()-60000;t.match={dragon::Family::Wifi,dragon::Confidence::None,"Manual/saved target; not live evidence"};}
bool targetAddAp(const uint8_t mac[6],uint8_t channel,const char* name){if(radioRunning||attackStatus.active||!unicastAddress(mac)||channel<1||channel>11||!validName(name))return false;int i=slot(mac);if(i<0)return false;auto& t=recon.targets[i];manual(t,mac);t.channel=channel;t.accessPoint=true;snprintf(t.name,sizeof(t.name),"%s",name);return true;}
bool targetAddStation(const uint8_t mac[6],unsigned index){if(radioRunning||attackStatus.active||index>=ReconCore::Capacity||!unicastAddress(mac))return false;auto& ap=recon.targets[index];if(!ap.used||ap.ble||!ap.accessPoint||!memcmp(mac,ap.mac,6))return false;int i=slot(mac);if(i<0)return false;auto& t=recon.targets[i];manual(t,mac);t.station=true;t.channel=ap.channel;memcpy(t.bssid,ap.mac,6);return true;}
bool targetRemove(unsigned i){if(radioRunning||attackStatus.active||i>=ReconCore::Capacity||int(i)==recon.pinned||int(i)==recon.selected)return false;recon.targets[i]={};return true;}
struct PsAllocator {void* allocate(size_t n){return ps_malloc(n);}void deallocate(void* p){free(p);}void* reallocate(void* p,size_t n){return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}};
using Doc=BasicJsonDocument<PsAllocator>;
static String address(const uint8_t mac[6]){char s[18];snprintf(s,sizeof(s),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);return s;}
bool targetsJsonSave(){
 if(!newPath("marauder-aps","json"))return false;Doc doc(32768);auto array=doc.to<JsonArray>();
 for(auto& t:recon.targets)if(t.used&&!t.ble&&t.accessPoint){auto a=array.createNestedObject();a["essid"]=t.name;a["channel"]=t.channel;a["bssid"]=address(t.mac);a["rssi"]=t.signal.value();a["packets"]=t.packets;a["sec"]=t.security;a["wps"]=t.ap.wps;a["man"]=t.ap.manufacturer;auto stations=a.createNestedArray("stations");for(auto& s:recon.targets)if(s.used&&!s.ble&&s.station&&!memcmp(s.bssid,t.mac,6))stations.add(address(s.mac));}
 if(doc.overflowed())return false;File f=SD.open(editorPath,FILE_WRITE);if(!f)return false;bool ok=serializeJson(doc,f)==measureJson(doc);f.flush();f.close();return ok;
}
bool targetsJsonLoad(const char* path){
 if(radioRunning||attackStatus.active||!validPath(path))return false;File f=SD.open(path);if(!f||f.isDirectory()||f.size()>32768){f.close();return false;}
 Doc doc(49152);auto err=deserializeJson(doc,f,DeserializationOption::NestingLimit(5));f.close();if(err||!doc.is<JsonArray>())return false;
 auto* staged=static_cast<Target*>(ps_calloc(ReconCore::Capacity,sizeof(Target)));if(!staged)return false;unsigned count=0;bool ok=true;
 for(JsonVariant v:doc.as<JsonArray>()){
   if(!v.is<JsonObject>()||count>=ReconCore::Capacity){ok=false;break;}
   const char* mac=v["bssid"]|"";const char* name=v["essid"]|"";int channel=v["channel"]|0;uint8_t bytes[6];
   if(!parseMac(mac,bytes)||channel<1||channel>11||strlen(name)>32||!unicastAddress(bytes)){ok=false;break;}
   for(unsigned i=0;i<count;++i)if(!memcmp(staged[i].mac,bytes,6))ok=false;if(!ok)break;
   auto& t=staged[count++];manual(t,bytes);t.channel=channel;t.accessPoint=true;t.security=v["sec"]|0;t.ap.wps=v["wps"]|false;snprintf(t.name,sizeof(t.name),"%s",name);snprintf(t.ap.manufacturer,sizeof(t.ap.manufacturer),"%s",v["man"]|"");
   if(!v["stations"].isNull()&&!v["stations"].is<JsonArray>()){ok=false;break;}
   for(JsonVariant sta:v["stations"].as<JsonArray>()){if(count==ReconCore::Capacity||!sta.is<const char*>()||!parseMac(sta.as<const char*>(),bytes)||!unicastAddress(bytes)){ok=false;break;}
     for(unsigned i=0;i<count;++i)if(!memcmp(staged[i].mac,bytes,6))ok=false;if(!ok)break;auto& s=staged[count++];manual(s,bytes);s.station=true;s.channel=channel;memcpy(s.bssid,t.mac,6);}
   if(!ok)break;
 }
 if(ok){memcpy(recon.targets,staged,sizeof(recon.targets));recon.selected=recon.pinned=-1;snprintf(editorPath,sizeof(editorPath),"%s",path);}free(staged);return ok;
}
bool radioMac(bool ap,const uint8_t* address){
 auto n=networkStatus();if(radioRunning||attackStatus.active||lab.mode!=LabMode::Off||n.connected||n.connecting||n.hotspot||n.busy||!radioPrepare())return false;
 uint8_t m[6];if(address)memcpy(m,address,6);else if(esp_read_mac(m,ap?ESP_MAC_WIFI_SOFTAP:ESP_MAC_WIFI_STA)!=ESP_OK)return false;
 uint8_t other[6]{};if(!unicastAddress(m)||esp_wifi_get_mac(ap?WIFI_IF_STA:WIFI_IF_AP,other)!=ESP_OK||!memcmp(m,other,6))return false;
 // IDF needs the selected mode present, with its interface stopped. NULL mode
 // rejects set_mac with ESP_ERR_WIFI_MODE even though get_mac still succeeds.
 auto stopped=esp_wifi_stop();if(stopped!=ESP_OK)return false;
 auto mode=esp_wifi_set_mode(ap?WIFI_MODE_AP:WIFI_MODE_STA);
 auto e=mode==ESP_OK?esp_wifi_set_mac(ap?WIFI_IF_AP:WIFI_IF_STA,m):mode;
 uint8_t actual[6]{};auto read=esp_wifi_get_mac(ap?WIFI_IF_AP:WIFI_IF_STA,actual);
 auto idle=esp_wifi_set_mode(WIFI_MODE_NULL);auto started=esp_wifi_start();
 snprintf(editorMessage,sizeof(editorMessage),"MAC set=%s read=%s restart=%s",esp_err_to_name(e),esp_err_to_name(read),esp_err_to_name(started));
 return e==ESP_OK&&read==ESP_OK&&idle==ESP_OK&&started==ESP_OK&&!memcmp(m,actual,6);
}
bool radioRandomMac(bool ap){uint8_t mac[6];esp_fill_random(mac,6);mac[0]=(mac[0]&0xfc)|2;return radioMac(ap,mac);}
unsigned markedAttackTargets(){if(attackStatus.active)return 0;auto previous=attackOptions.target;unsigned added=0;for(auto& t:recon.targets)if(t.used&&t.marked&&!t.ble&&t.accessPoint&&t.channel>=1&&t.channel<=11){
  AttackTarget candidate;candidate.hasAp=true;memcpy(candidate.bssid,t.mac,6);candidate.channel=t.channel;snprintf(candidate.ssid,sizeof(candidate.ssid),"%.32s",t.name);bool clients=false;
  for(auto& c:recon.targets)if(c.used&&c.marked&&!c.ble&&c.station&&!memcmp(c.bssid,t.mac,6)){candidate.hasClient=true;memcpy(candidate.client,c.mac,6);attackOptions.target=candidate;unsigned before=attackListCount();if(attackListAdd()&&attackListCount()>before)++added;clients=true;}
  if(!clients){attackOptions.target=candidate;unsigned before=attackListCount();if(attackListAdd()&&attackListCount()>before)++added;}
 }attackOptions.target=previous;return added;}
bool editorCommand(const char* line){
 bool ok=false;char extra;unsigned i=0;uint8_t mac[6];char macString[20],name[33];unsigned channel=0;
 if(!strcmp(line,"SSIDS")){for(unsigned j=0;j<count;++j)Serial.printf("HP_SSID %u %s\n",j,names[j]);Serial.printf("HP_SSIDS_END count=%u\n",count);return true;}
 else if(!strncmp(line,"SSID ADD ",9))ok=ssidAdd(line+9);
 else if(sscanf(line,"SSID REMOVE %u%c",&i,&extra)==1)ok=ssidRemove(i);
 else if(sscanf(line,"SSID GENERATE %u%c",&i,&extra)==1)ok=ssidGenerate(i);
 else if(!strcmp(line,"SSID CLEAR")){ssidClear();ok=true;}
 else if(!strcmp(line,"SSID SAVE"))ok=ssidSave();
 else if(!strncmp(line,"SSID LOAD ",10))ok=ssidLoad(line+10);
 else if(!strcmp(line,"LIST EXPORTJSON"))ok=targetsJsonSave();
 else if(!strncmp(line,"LIST IMPORTJSON ",16))ok=targetsJsonLoad(line+16);
 else if(sscanf(line,"LIST REMOVE %u%c",&i,&extra)==1)ok=targetRemove(i);
 else if(sscanf(line,"ADD AP %19s %u %32[^\n]",macString,&channel,name)==3)ok=parseMac(macString,mac)&&channel<=11&&targetAddAp(mac,channel,name);
 else if(sscanf(line,"ADD STATION %19s %u%c",macString,&i,&extra)==2)ok=parseMac(macString,mac)&&targetAddStation(mac,i);
 else if(!strcmp(line,"MAC STATUS")){uint8_t ap[6]{},sta[6]{};radioPrepare();esp_wifi_get_mac(WIFI_IF_AP,ap);esp_wifi_get_mac(WIFI_IF_STA,sta);Serial.printf("HP_MAC ap=%s sta=%s\n",address(ap).c_str(),address(sta).c_str());return true;}
 else if(!strcmp(line,"MAC RANDOM AP"))ok=radioRandomMac(true);
 else if(!strcmp(line,"MAC RANDOM STA"))ok=radioRandomMac(false);
 else if(!strcmp(line,"MAC RESET AP"))ok=radioMac(true,nullptr);
 else if(!strcmp(line,"MAC RESET STA"))ok=radioMac(false,nullptr);
 else if(!strncmp(line,"MAC AP ",7))ok=parseMac(line+7,mac)&&radioMac(true,mac);
 else if(!strncmp(line,"MAC STA ",8))ok=parseMac(line+8,mac)&&radioMac(false,mac);
 else if(!strcmp(line,"SELECT ALL")||!strcmp(line,"SELECT NONE")){bool on=!strcmp(line,"SELECT ALL");for(auto& t:recon.targets)if(t.used)t.marked=on;ok=true;}
 else if(sscanf(line,"SELECT %u%c",&i,&extra)==1&&i<ReconCore::Capacity){auto& t=recon.targets[i];if(t.used){t.marked=!t.marked;ok=true;}}
 else if(!strncmp(line,"SELECT CONTAINS ",16)){for(auto& t:recon.targets)if(t.used&&strstr(t.name,line+16)){t.marked=true;ok=true;}}
 else if(!strcmp(line,"SELECT LIST")){for(unsigned j=0;j<ReconCore::Capacity;++j)if(recon.targets[j].used&&recon.targets[j].marked)Serial.printf("HP_SELECTED %u %s\n",j,recon.targets[j].name);Serial.println("HP_SELECTED_END");return true;}
 else if(!strcmp(line,"ATTACK MARKED")){Serial.printf("HP_EDITOR added=%u total=%u\n",markedAttackTargets(),attackListCount());return true;}
 else if(!strcmp(line,"ATTACK CLIENTS")){unsigned n=attackListObservedClients();Serial.printf("HP_EDITOR added=%u total=%u\n",n,attackListCount());return true;}
 else if(sscanf(line,"ATTACK REMOVE %u%c",&i,&extra)==1)ok=attackListRemove(i);
 else return false;
 Serial.printf("HP_EDITOR %s path=%s detail=%s\n",ok?"OK":"FAILED",editorPath,editorMessage);return true;
}
}
