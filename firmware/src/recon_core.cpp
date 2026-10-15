#include "recon_core.h"
#include <limits.h>

namespace hackerphone {
const dragon::DroneInfo* ReconCore::drone(const Target& t)const {
  for(const auto& d:drones_)if(d.used&&d.ble==t.ble&&d.type==t.addressType&&!memcmp(d.mac,t.mac,6))return &d.info;return nullptr;
}
void ReconCore::drone(const Target& t,const dragon::DroneInfo& info,uint32_t now){
  if(!info.valid)return;unsigned slot=0;uint32_t age=0;int unused=-1;
  for(unsigned i=0;i<8;++i){auto& d=drones_[i];if(d.used&&d.ble==t.ble&&d.type==t.addressType&&!memcmp(d.mac,t.mac,6)){dragon::mergeDrone(d.info,info,now);d.at=now;return;}
    if(!d.used&&unused<0)unused=i;if(uint32_t(now-d.at)>=age){age=now-d.at;slot=i;}}
  if(unused>=0)slot=unused;auto& d=drones_[slot];d={};d.used=true;d.ble=t.ble;d.type=t.addressType;memcpy(d.mac,t.mac,6);d.at=now;dragon::mergeDrone(d.info,info,now);
}
int ReconCore::observe(const uint8_t* mac,bool ble,uint8_t type,const char* name,dragon::Match match,int rssi,uint8_t channel,uint32_t now) {
  int index=-1,free=-1,oldest=-1;uint32_t age=0;
  for(size_t i=0;i<Capacity;++i){auto& t=targets[i];
    if(t.used&&t.ble==ble&&t.addressType==type&&!memcmp(t.mac,mac,6)){index=int(i);break;}
    if(!t.used&&free<0)free=int(i);
    if(int(i)!=selected&&int(i)!=pinned&&!t.marked&&(oldest<0||uint32_t(now-t.last)>age)){oldest=int(i);age=uint32_t(now-t.last);}
  }
  if(index<0){index=free>=0?free:oldest;if(index<0)return -1;
    auto& t=targets[index];if(t.used)++evictions;t={};t.used=true;t.ble=ble;t.addressType=type;t.first=now;
    memcpy(t.mac,mac,6);t.match={ble?dragon::Family::Ble:dragon::Family::Wifi,dragon::Confidence::None,"No signature"};
  }
  auto& t=targets[index];
  if(t.packets&&uint32_t(now-t.last)>30000){t.signal={};t.matches=0;t.match={ble?dragon::Family::Ble:dragon::Family::Wifi,dragon::Confidence::None,"Expired evidence"};}
  if(t.matches&&uint32_t(now-t.matchedAt)>30000){t.matches=0;t.match={ble?dragon::Family::Ble:dragon::Family::Wifi,dragon::Confidence::None,"Expired evidence"};}
  t.last=now;t.channel=channel;if(t.packets<UINT32_MAX)++t.packets;t.signal.add(rssi,now);
  t.peak=std::max<int16_t>(t.peak,rssi);t.history.add(t.signal.value(),now);
  if(name&&*name)snprintf(t.name,sizeof(t.name),"%s",name);
  if(match.confidence!=dragon::Confidence::None&&match.confidence>=t.match.confidence){
    if(match.family!=t.match.family||match.confidence!=t.match.confidence)t.matches=0;
    t.match=match;t.matchedAt=now;if(t.matches<UINT16_MAX)++t.matches;
  }
  return index;
}
int ReconCore::wifi(const uint8_t* data,size_t n,int rssi,uint8_t channel,uint32_t now){
  ++wifiFrames;if(!data||n<10||(data[0]&3)){++malformed;return -1;}
  if(channel>=1&&channel<=11)++channelFrames[channel];
  unsigned type=(data[0]>>2)&3;
  if(type==1){++controlFrames;return -1;}
  if(type==0)++managementFrames;else if(type==2)++dataFrames;
  if(n<24){++malformed;return -1;}
  auto f=dragon::parseWifi(data,n);if(!f.valid){++malformed;return -1;}
  if(f.beacon&&f.match.confidence==dragon::Confidence::None&&pineappleCandidate(data,n))
    f.match={dragon::Family::Pineapple,dragon::Confidence::Low,"Marauder PineScan vendor/beacon heuristic; shared hardware, not proof of Pineapple"};
  // Adjacent-channel leakage is common at strong RSSI. An AP's DS channel IE
  // takes precedence over the receiver's current tuning. Data frames inherit
  // the known associated AP's channel instead of overwriting it with leakage.
  uint8_t actualChannel=channel;
  if(f.beacon&&f.channel>=1&&f.channel<=11)actualChannel=f.channel;
  else{const uint8_t* ap=f.bssid;if((data[0]&12)==8){unsigned ds=data[1]&3;ap=ds==1?data+4:ds==2?data+10:nullptr;}
    if(ap)for(const auto& known:targets)if(known.used&&!known.ble&&known.accessPoint&&known.ap.interval&&known.fresh(now)&&!memcmp(known.mac,ap,6)){actualChannel=known.channel;break;}}
  if((f.deauth||f.disassoc)&&retries_.duplicate(f,now))return -1;
  int i=observe(f.source,false,0,f.name,f.match,rssi,actualChannel,now);if(i<0)return i;
  auto& t=targets[i];drone(t,f.drone,now);if(f.beacon){t.accessPoint=true;t.security=f.security;memcpy(t.bssid,f.bssid,6);updateApDetails(t.ap,data,n);++beacons;}
  if(f.probe){t.probe=t.station=true;++probes;}
  if((data[0]&12)==8){
    // DS bits identify transmitter roles; WDS is not a client/AP inference.
    if((data[1]&3)==1){t.station=true;memcpy(t.bssid,data+4,6);}
    else if((data[1]&3)==2){t.accessPoint=true;memcpy(t.bssid,data+10,6);}
  }
  if(f.eapol){++eapolFrames;++t.eapol;eapol.observe(data,n,now);}
  if(data[0]==0xb0&&n>=30&&data[24]==3&&data[25]==0){++saeFrames;++t.sae;}
  uint8_t peer[6];if(ssids_.observe(f,now,peer)){++ssidAlerts;t.alerts|=4;event(dragon::Family::EvilTwin,"Same SSID, different security; verify configuration",now);}
  if(f.deauth||f.disassoc){
    ++t.disconnects;
    ++traffic.disconnectFrames;
    if(disconnects_.observe(now)){
      ++traffic.disconnectAlerts;traffic.lastDisconnect=now;traffic.channel=channel;traffic.reason=f.reason;traffic.protectedFrame=f.protectedFrame;t.alerts|=1;
      event(dragon::Family::Deauth,"Disconnect burst; source spoofable, attack unconfirmed",now);
    }
  }
  return i;
}
int ReconCore::ble(const uint8_t* data,size_t n,const uint8_t* mac,uint8_t type,int rssi,uint32_t now){
  ++bleFrames;if(!data||!mac||n>255){++malformed;return -1;}
  auto f=dragon::parseBle(data,n);if(!f.valid){++malformed;return -1;}
  auto match=dragon::addressHint(mac,type==0,f.match);
  if(match.confidence==dragon::Confidence::None&&marauderMetaCandidate(data,n))match={dragon::Family::Meta,dragon::Confidence::Low,"Marauder Meta identifier heuristic; glasses and recording not established"};
  int i=observe(mac,true,type,f.name,match,rssi,0,now);if(i<0)return i;
  auto& t=targets[i];drone(t,f.drone,now);t.company=f.company;t.advertSize=std::min(n,sizeof(t.advert));memcpy(t.advert,data,t.advertSize);
  uint32_t hash=dragon::crc32(data,n);if(t.packets>1&&t.advertHash!=hash)++t.advertChanges;t.advertHash=hash;
  dragon::DeviceKey key;memcpy(key.mac,mac,6);key.radio=1;key.addressType=type;
  auto check=[&](unsigned group){auto b=bursts_.observe(key,group,dragon::crc32(data,n),now);if(b.alert){
    ++traffic.bleAlerts;traffic.lastBle=now;traffic.bleGroup=group;traffic.bleAddresses=b.addresses;traffic.bleVariants=b.variants;t.alerts|=2;
    event(dragon::Family::BleSpam,"BLE pairing/beacon burst; possible spam or busy area",now);
  }};
  if(f.applePopup)check(0);if(f.fastPair)check(1);if(f.swiftPair)check(2);
  if(match.family==dragon::Family::Tracker||match.family==dragon::Family::SmartTag||match.family==dragon::Family::GoogleTag||match.family==dragon::Family::Tile||match.family==dragon::Family::IBeacon)check(3);
  return i;
}
void ReconCore::event(dragon::Family family,const char* text,uint32_t now){auto& e=events[eventCount++%16];e.at=now;e.family=family;snprintf(e.text,sizeof(e.text),"%s",text);}
size_t ReconCore::count(uint32_t now,bool signaturesOnly)const {
  size_t n=0;for(auto& t:targets)if(t.fresh(now)&&(!signaturesOnly||t.match.confidence!=dragon::Confidence::None))++n;return n;
}
}
