#include "recon_core.h"
#include <limits.h>

namespace hackerphone {
int ReconCore::observe(const uint8_t* mac,bool ble,uint8_t type,const char* name,dragon::Match match,int rssi,uint8_t channel,uint32_t now) {
  int index=-1,free=-1,oldest=-1;uint32_t age=0;
  for(size_t i=0;i<Capacity;++i){auto& t=targets[i];
    if(t.used&&t.ble==ble&&t.addressType==type&&!memcmp(t.mac,mac,6)){index=int(i);break;}
    if(!t.used&&free<0)free=int(i);
    if(int(i)!=selected&&int(i)!=pinned&&(oldest<0||uint32_t(now-t.last)>age)){oldest=int(i);age=uint32_t(now-t.last);}
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
  ++wifiFrames;if(!data||n<24){++malformed;return -1;}
  if(channel>=1&&channel<=11)++channelFrames[channel];
  auto f=dragon::parseWifi(data,n);if(!f.valid){++malformed;return -1;}
  if((f.deauth||f.disassoc)&&retries_.duplicate(f,now))return -1;
  int i=observe(f.source,false,0,f.name,f.match,rssi,channel,now);if(i<0)return i;
  auto& t=targets[i];if(f.beacon){t.accessPoint=true;t.security=f.security;memcpy(t.bssid,f.bssid,6);}
  if(f.probe){t.probe=t.station=true;++probes;}
  if((data[0]&12)==8){
    // DS bits identify transmitter roles; WDS is not a client/AP inference.
    if((data[1]&3)==1){t.station=true;memcpy(t.bssid,data+4,6);}
    else if((data[1]&3)==2){t.accessPoint=true;memcpy(t.bssid,data+10,6);}
  }
  if(f.eapol)++eapolFrames;
  uint8_t peer[6];if(ssids_.observe(f,now,peer)){++ssidAlerts;t.alerts|=4;event(dragon::Family::EvilTwin,"Same SSID, different security; verify configuration",now);}
  if(f.deauth||f.disassoc){
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
  int i=observe(mac,true,type,f.name,match,rssi,0,now);if(i<0)return i;
  auto& t=targets[i];t.company=f.company;t.advertSize=std::min(n,sizeof(t.advert));memcpy(t.advert,data,t.advertSize);
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
