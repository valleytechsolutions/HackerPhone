#include "mission_service.h"
#include "mp24_board.h"
#include "radio_service.h"
#include "capture_format.h"
#include "attack_frames.h"
#include <ArduinoJson.h>
namespace hackerphone {
MissionStatus mission;static File log;static uint32_t lastTick=0,lastFlush=0,lastEvent=0;
struct Seen {bool used=false,ble=false,departed=false;uint8_t mac[6]{},type=0;uint32_t packets=0,at=0;uint8_t bssid[6]{};};
static Seen seen[ReconCore::Capacity]{};static RadioMode previousMode;static uint8_t previousLock=0;
static bool record(JsonDocument& doc){size_t expected=measureJson(doc);size_t n=serializeJson(doc,log);bool ok=n==expected&&log.write('\n')==1;mission.bytes+=n+1;++mission.records;if(!ok){mission.error=true;snprintf(mission.message,sizeof(mission.message),"SD write failed; mission stopped");}return ok;}
static String mac(const uint8_t* m){char s[18];snprintf(s,sizeof(s),"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]);return s;}
static void manifest(bool complete){
 String path=String(mission.path)+"/manifest.json";File f=SD.open(path,FILE_WRITE);if(!f){mission.error=true;return;}
 StaticJsonDocument<768> doc;doc["format"]="HackerPhone mission 1";doc["complete"]=complete&&!mission.error;doc["started_uptime_ms"]=mission.started;doc["duration_ms"]=millis()-mission.started;doc["radio"]=unsigned(radioMode);doc["records"]=mission.records;doc["observations"]=mission.observed;doc["relationships"]=mission.relationships;doc["probes"]=mission.probes;doc["alerts"]=mission.alerts;doc["error"]=mission.error;doc["location"]=nullptr;doc["log"]="events.jsonl";doc["tracking_limit"]=64;
 if(serializeJson(doc,f)!=measureJson(doc))mission.error=true;f.flush();f.close();
}
bool missionStart(unsigned mode){
 if(mode>2||mission.active||!mp24::cardReady)return false;
 SD.mkdir("/hackerphone");SD.mkdir("/hackerphone/missions");char path[96];bool available=false;
 for(unsigned i=1;i<100000;++i){snprintf(path,sizeof(path),"/hackerphone/missions/mission-%05u",i);if(!SD.exists(path)){available=SD.mkdir(path);break;}}
 if(!available)return false;log=SD.open(String(path)+"/events.jsonl",FILE_WRITE);if(!log)return false;
 previousMode=radioMode;previousLock=channelLock;radioSetMode(RadioMode(mode));if(!radioStart()){log.close();radioSetMode(previousMode);return false;}
 mission={};mission.active=true;mission.started=millis();snprintf(mission.path,sizeof(mission.path),"%s",path);snprintf(mission.message,sizeof(mission.message),"Recording observations / links");memset(seen,0,sizeof(seen));lastTick=lastFlush=mission.started;lastEvent=recon.eventCount;manifest(false);return true;
}
void missionStop(){if(!mission.active)return;log.flush();log.close();manifest(true);mission.active=false;radioStop();channelLock=previousLock;radioSetMode(previousMode);if(!mission.error)snprintf(mission.message,sizeof(mission.message),"Mission saved on SD");}
void missionTick(uint32_t now){
 if(!mission.active)return;if(!radioRunning||mission.error){missionStop();return;}if(uint32_t(now-lastTick)<1000)return;lastTick=now;
 // Bounded per-second snapshots keep packet reception free of SD writes.
 for(unsigned i=0;i<ReconCore::Capacity;++i){auto& t=recon.targets[i];auto& s=seen[i];bool same=s.used&&t.used&&s.ble==t.ble&&s.type==t.addressType&&!memcmp(s.mac,t.mac,6);
   if(s.used&&(!same||!t.fresh(now))&&!s.departed){StaticJsonDocument<256> d;d["event"]="departed";d["uptime_ms"]=now;d["mac"]=mac(s.mac);d["radio"]=s.ble?"BLE":"WiFi";record(d);s.departed=true;}
   if(!t.fresh(now))continue;
   if(same&&s.packets==t.packets)continue;if(same&&!s.departed&&uint32_t(now-s.at)<5000)continue;
   StaticJsonDocument<1024> d;d["event"]=same&&!s.departed?"repeat":"observed";d["uptime_ms"]=now;d["mac"]=mac(t.mac);d["radio"]=t.ble?"BLE":"WiFi";d["address_type"]=t.addressType;d["channel"]=t.channel;d["rssi"]=t.signal.value();d["packets"]=t.packets;d["family"]=dragon::familyName(t.match.family);d["confidence"]=unsigned(t.match.confidence);d["name"]=t.name;d["evidence"]=t.match.evidence;d["ap"]=t.accessPoint;d["station"]=t.station;d["probe"]=t.probe;
   if(t.station&&unicastAddress(t.bssid)){d["associated_ap"]=mac(t.bssid);if(!same||memcmp(s.bssid,t.bssid,6))++mission.relationships;}
   if(t.probe&&(!same||t.packets!=s.packets))++mission.probes;
   if(!same||s.departed)++mission.observed;
   if(!record(d))break;s.used=true;s.ble=t.ble;s.type=t.addressType;s.departed=false;memcpy(s.mac,t.mac,6);memcpy(s.bssid,t.bssid,6);s.packets=t.packets;s.at=now;
 }
 if(recon.eventCount-lastEvent>16)lastEvent=recon.eventCount-16;
 while(lastEvent<recon.eventCount){auto& e=recon.events[lastEvent%16];StaticJsonDocument<384> d;d["event"]="alert";d["uptime_ms"]=e.at;d["family"]=dragon::familyName(e.family);d["detail"]=e.text;record(d);++mission.alerts;++lastEvent;}
 if(uint32_t(now-lastFlush)>=5000){log.flush();lastFlush=now;}
 if(mission.error)missionStop();
}
}
