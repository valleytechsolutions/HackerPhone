#pragma once
#include <stdint.h>
#include <stddef.h>
namespace hackerphone {
enum class NetworkProbe:uint8_t {Ping,PingSweep,Arp,CommonPorts,AllPorts,Telnet,Ssh,Dns,Http,Https,Smtp,Rdp,Count};
extern const char* networkProbeNames[unsigned(NetworkProbe::Count)];
struct NetworkStatus {
  bool connecting=false,connected=false,hotspot=false,portal=false,busy=false,truncated=false,interfaceUp=false,tethered=false;
  uint32_t address=0,mask=0,gateway=0,tested=0,found=0,visits=0;
  uint32_t forms=0,formsSaved=0,formsLost=0;
  uint16_t rows=0;int reason=0;char message[64]="Join a network first";
};
bool networkJoin(const char* ssid,const char* password);
bool networkHotspot(const char* ssid,const char* password,bool portal);
bool networkTether(bool enable,uint32_t dns=0);
bool networkProbe(NetworkProbe mode,const char* host);
void networkStop();
void networkCancelProbe();
void networkTick(uint32_t now);
NetworkStatus networkStatus();
bool networkRow(unsigned index,char* output,size_t capacity);
bool networkSaveResults();
bool networkSelfTest();
bool networkPortalTest();
bool networkClaimExternal();
void networkReleaseExternal(const char* message);
}
