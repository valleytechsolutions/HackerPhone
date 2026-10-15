#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hackerphone {
struct Target;
enum class ScanProfile:uint8_t {
  All,AccessPoints,Stations,Probes,Ble,Espressif,Pwnagotchi,Pineapple,MultiSsid,
  FindMy,Flipper,Skimmer,Flock,Glasses,IBeacon,Drone,Axon,Trackers,Ring,
  Eapol,Disconnects,Sae,Count
};
struct ScanDefinition {const char* name;uint8_t radio;}; // 0 combined, 1 WiFi, 2 BLE
extern const ScanDefinition scanDefinitions[unsigned(ScanProfile::Count)];
bool scanMatches(const Target& target,ScanProfile profile);
struct ApDetails {
  uint16_t capability=0,interval=0;
  bool wps=false,pmfCapable=false,pmfRequired=false,sae=false;
  uint8_t tags=0,ssidCount=0,ssidSizes[3]{};
  uint8_t ssids[3][32]{};
  char manufacturer[32]{};
};
bool updateApDetails(ApDetails& details,const uint8_t* frame,size_t length);
bool espressifAddress(const uint8_t mac[6]);
bool pineappleCandidate(const uint8_t* frame,size_t length);
bool marauderMetaCandidate(const uint8_t* advert,size_t length);
// Validate a challenge against both endpoints; return bounded token length.
size_t saeChallenge(const uint8_t* frame,size_t length,const uint8_t ap[6],
                    const uint8_t client[6],uint8_t* token,size_t capacity);
// Preserve a captured beacon's capabilities, security and vendor IEs.
// replacementIe=0 copies it; 37/40 replace CSA/Quiet. Failure never writes output.
size_t cloneBeacon(uint8_t* out,size_t capacity,const uint8_t* beacon,size_t size,
                   uint16_t sequence,uint8_t replacementIe=0,uint8_t destination=1);
}
