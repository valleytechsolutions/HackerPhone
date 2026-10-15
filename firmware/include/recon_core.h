#pragma once
#include "detection.h"
#include "tracking.h"
#include "traffic.h"
#include "scan_profiles.h"
#include "eapol_tracker.h"

namespace hackerphone {
struct Target {
  bool used=false, ble=false;
  uint8_t mac[6]{},addressType=0,channel=0;
  char name[64]{};
  dragon::Match match{dragon::Family::Wifi,dragon::Confidence::None,"No signature"};
  dragon::SignalTrack signal;
  uint32_t first=0,last=0,matchedAt=0,packets=0;
  uint16_t matches=0;
  bool accessPoint=false,station=false,probe=false;
  uint8_t security=0,bssid[6]{},advert[96]{},advertSize=0;
  uint16_t company=0xffff;uint8_t alerts=0;
  ApDetails ap;
  uint32_t eapol=0,disconnects=0,sae=0;
  uint32_t advertHash=0,advertChanges=0;
  int16_t peak=-127;
  dragon::SignalHistory history;
  bool fresh(uint32_t now)const {return used&&uint32_t(now-last)<15000;}
};
class ReconCore {
 public:
  static constexpr size_t Capacity=64;
  Target targets[Capacity]{};
  uint32_t wifiFrames=0,bleFrames=0,malformed=0,evictions=0;
  int selected=-1,pinned=-1;
  uint32_t channelFrames[12]{},channelMillis[12]{},eapolFrames=0,probes=0,ssidAlerts=0;
  uint32_t managementFrames=0,dataFrames=0,controlFrames=0,beacons=0,saeFrames=0;
  EapolTracker eapol;
  dragon::TrafficStatus traffic;
  struct Event {uint32_t at=0;dragon::Family family=dragon::Family::Wifi;char text[84]{};};
  Event events[16]{};uint32_t eventCount=0;
  int wifi(const uint8_t* data,size_t n,int rssi,uint8_t channel,uint32_t now);
  int ble(const uint8_t* data,size_t n,const uint8_t* mac,uint8_t type,int rssi,uint32_t now);
  size_t count(uint32_t now,bool signaturesOnly=false)const;
  const dragon::DroneInfo* drone(const Target& target)const;
  void event(dragon::Family family,const char* text,uint32_t now);
 private:
  struct DroneObservation {bool used=false,ble=false;uint8_t mac[6]{},type=0;uint32_t at=0;dragon::DroneInfo info;};
  DroneObservation drones_[8]{};
  void drone(const Target& target,const dragon::DroneInfo& info,uint32_t now);
  dragon::WifiRetryFilter retries_;
  dragon::DisconnectMonitor disconnects_;
  dragon::SsidMonitor ssids_;
  dragon::BleBurstMonitor bursts_;
  int observe(const uint8_t* mac,bool ble,uint8_t type,const char* name,dragon::Match match,int rssi,uint8_t channel,uint32_t now);
};
}
