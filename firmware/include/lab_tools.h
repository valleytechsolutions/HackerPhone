#pragma once
#include <stdint.h>
#include <stddef.h>
namespace hackerphone {
enum class LabMode:uint8_t{Off,WifiBeacon,BleBeacon,BleReplay,BleIdentityReplay};
struct LabStatus{LabMode mode=LabMode::Off;uint32_t started=0,accepted=0,errors=0;char message[48]="Idle";};
extern LabStatus lab;
bool labStart(LabMode mode,uint8_t channel,const uint8_t* advert=nullptr,size_t n=0,const uint8_t* mac=nullptr,uint8_t addressType=0);
void labStop();
void labTick(uint32_t now);
size_t makeBeacon(uint8_t* out,size_t capacity,const uint8_t mac[6],uint8_t channel,const char* ssid);
bool validLegacyAdvert(const uint8_t* bytes,size_t n);
}
