#include "scan_profiles.h"
#include "recon_core.h"
#include <string.h>
namespace hackerphone {
const ScanDefinition scanDefinitions[]={
  {"All devices",0},{"WiFi access points",1},{"WiFi stations",1},{"Probe requests",1},
  {"All BLE devices",2},{"Espressif devices",1},{"Pwnagotchi peers",1},{"Pineapple candidates",1},
  {"Multi-SSID BSSIDs",1},{"Find My / AirTags",2},{"Flipper Zero",2},{"Serial / skimmer clues",2},
  {"Flock cameras",0},{"Meta / Ray-Ban",2},{"iBeacons",2},{"Drone Remote ID",0},
  {"Axon / TASER",0},{"All tracker families",2},{"Ring cameras",0},
  {"EAPOL participants",1},{"Disconnect senders",1},{"SAE authentication",1}
};
static_assert(sizeof(scanDefinitions)/sizeof(*scanDefinitions)==unsigned(ScanProfile::Count));
bool espressifAddress(const uint8_t* m){
  if(!m||(m[0]&3))return false;
  // Public vendor prefixes, never a malicious-device classification.
  const uint32_t prefixes[]={0x240ac4,0x246f28,0x24d7eb,0x30aea4,0x3c6105,0x3c71bf,0x7c9ebd,0x84cca8,0x84f3eb,0x8caab5,0xa020a6,0xa4cf12,0xac67b2,0xb4e62d,0xbcddc2,0xc44f33,0xcc50e3,0xd8a01d,0xdc4f22,0xecfabc,0x483fda,0x40f520,0x806599,0x94b97e,0x34865d,0x588c81,0x64e833,0x7cdfa1,0x10521c,0x08d1f9,0x34b472,0x30c6f7,0x441793,0x24ec4a,0x288485};
  uint32_t p=uint32_t(m[0])<<16|uint32_t(m[1])<<8|m[2];for(auto x:prefixes)if(x==p)return true;return false;
}
bool marauderMetaCandidate(const uint8_t* p,size_t n){
  if(!p)return false;bool meta=false,blocked=false;
  auto check=[&](uint16_t id){meta|=id==0xfd5f||id==0xfeb7||id==0xfeb8||id==0x01ab||id==0x058e||id==0x0d53;
    blocked|=id==0xfd5a||id==0xfd69||id==0x004c||id==0x0006||id==0xfef3;};
  for(size_t at=0;at<n;){unsigned length=p[at++];if(!length)break;if(length>n-at)return false;unsigned type=p[at];const uint8_t* d=p+at+1;size_t size=length-1;
    if(type==0xff&&size>=2)check(d[0]|uint16_t(d[1])<<8);
    size_t width=(type==2||type==3||type==0x16)?2:(type==4||type==5||type==0x20)?4:(type==6||type==7||type==0x21)?16:0;
    if(width)for(size_t offset=0;offset+width<=size;offset+=width){uint16_t id;if(dragon::assignedUuid16(d+offset,width,id))check(id);if(type>=0x16)break;}at+=length;
  }return meta&&!blocked;
}
bool scanMatches(const Target& t,ScanProfile p){
  if(!t.used)return false;using F=dragon::Family;auto f=t.match.family;
  switch(p){
    case ScanProfile::All:return true;case ScanProfile::AccessPoints:return !t.ble&&t.accessPoint;
    case ScanProfile::Stations:return !t.ble&&t.station;case ScanProfile::Probes:return !t.ble&&t.probe;
    case ScanProfile::Ble:return t.ble;case ScanProfile::Espressif:return !t.ble&&espressifAddress(t.mac);
    case ScanProfile::Pwnagotchi:return f==F::Pwnagotchi;case ScanProfile::Pineapple:return f==F::Pineapple;
    case ScanProfile::MultiSsid:return !t.ble&&t.ap.ssidCount>=3;
    case ScanProfile::FindMy:return f==F::Tracker;case ScanProfile::Flipper:return f==F::Flipper;
    case ScanProfile::Skimmer:return f==F::Skimmer;case ScanProfile::Flock:return f==F::Flock;
    case ScanProfile::Glasses:return f==F::Meta;case ScanProfile::IBeacon:return f==F::IBeacon;
    case ScanProfile::Drone:return f==F::Drone;case ScanProfile::Axon:return f==F::Axon;
    case ScanProfile::Trackers:return f==F::Tracker||f==F::SmartTag||f==F::GoogleTag||f==F::Tile;
    case ScanProfile::Ring:return f==F::Ring;case ScanProfile::Eapol:return t.eapol>0;
    case ScanProfile::Disconnects:return t.disconnects>0;case ScanProfile::Sae:return t.sae>0;
    default:return false;
  }
}
static uint16_t le16(const uint8_t* p){return p[0]|uint16_t(p[1])<<8;}
static uint16_t be16(const uint8_t* p){return uint16_t(p[0])<<8|p[1];}
static bool beaconValid(const uint8_t* p,size_t n){
  if(!p||n<36||(p[0]!=0x80&&p[0]!=0x50))return false;
  size_t at=36;while(at<n){if(n-at<2||p[at+1]>n-at-2)return false;at+=2+p[at+1];}return true;
}
bool pineappleCandidate(const uint8_t* p,size_t n){
  if(!beaconValid(p,n))return false;
  const uint32_t oui=uint32_t(p[10])<<16|uint32_t(p[11])<<8|p[12];
  const bool privacy=p[34]&16;
  const uint32_t always[]={0x001337,0x000a00,0x000c43,0x000ce7,0x0017a5,0x9cefd5,0x9ce5d5,0xdeadbe};
  for(auto x:always)if(x==oui)return true;
  if(!privacy&&(oui==0x00c0ca||oui==0x1cbfce||oui==0x0cefaf))return true;
  if(privacy&&(oui==0x02c0ca||oui==0x021337))return true;
  return n>=41&&le16(p+34)==1&&p[36]==0&&p[37]<=32&&n==size_t(41+p[37])&&p[38+p[37]]==3&&p[39+p[37]]==1;
}
bool updateApDetails(ApDetails& a,const uint8_t* p,size_t n){
  if(!beaconValid(p,n))return false;
  a.capability=le16(p+34);a.interval=le16(p+32);a.tags=0;a.wps=a.pmfCapable=a.pmfRequired=a.sae=false;
  a.manufacturer[0]=0;
  for(size_t at=36;at<n;){unsigned id=p[at],len=p[at+1];auto d=p+at+2;at+=len+2;if(a.tags<255)++a.tags;
    if(id==0&&len&&len<=32){bool found=false;for(unsigned i=0;i<a.ssidCount;++i)if(a.ssidSizes[i]==len&&!memcmp(a.ssids[i],d,len))found=true;
      if(!found&&a.ssidCount<3){unsigned i=a.ssidCount++;a.ssidSizes[i]=len;memcpy(a.ssids[i],d,len);}}
    if(id==48&&len>=8){size_t off=6;
      if(len-off<2)continue;size_t count=le16(d+off);off+=2;if(count>(len-off)/4)continue;off+=count*4;
      if(len-off<2)continue;count=le16(d+off);off+=2;if(count>(len-off)/4)continue;
      for(unsigned i=0;i<count;++i){auto suite=d+off+i*4;if(!memcmp(suite,"\x00\x0f\xac",3)&&(suite[3]==8||suite[3]==9))a.sae=true;}off+=count*4;
      if(len-off>=2){auto caps=le16(d+off);a.pmfRequired=caps&64;a.pmfCapable=caps&128;}}
    if(id==221&&len>=4&&!memcmp(d,"\x00\x50\xf2\x04",4)){
      a.wps=true;for(size_t off=4;off+4<=len;){auto tag=be16(d+off),size=be16(d+off+2);off+=4;if(size>len-off)break;
        if(tag==0x1021)dragon::safeText(a.manufacturer,sizeof(a.manufacturer),d+off,size);off+=size;}}
  }return true;
}
size_t saeChallenge(const uint8_t* p,size_t n,const uint8_t ap[6],const uint8_t client[6],uint8_t* out,size_t cap){
  if(!p||!ap||!client||!out||n<=32||n-32>cap||n-32>128||p[0]!=0xb0||(p[1]&0x40)||
     memcmp(p+4,client,6)||memcmp(p+10,ap,6)||memcmp(p+16,ap,6)||le16(p+24)!=3||le16(p+26)!=1||le16(p+28)!=76||le16(p+30)!=19)return 0;
  memcpy(out,p+32,n-32);return n-32;
}
size_t cloneBeacon(uint8_t* out,size_t cap,const uint8_t* p,size_t n,uint16_t seq,uint8_t id,uint8_t destination){
  if(!out||out==p||!beaconValid(p,n)||n>1024||(id&&id!=37&&id!=40)||(id==37&&(destination<1||destination>11)))return 0;
  size_t needed=36;for(size_t at=36;at<n;at+=2+p[at+1])if(!id||p[at]!=id)needed+=2+p[at+1];
  needed+=id==37?5:id==40?8:0;if(needed>cap)return 0;
  memcpy(out,p,36);out[0]=0x80;out[1]=0;memset(out+4,255,6);out[22]=(seq&15)<<4;out[23]=seq>>4;
  size_t pos=36;for(size_t at=36;at<n;at+=2+p[at+1])if(!id||p[at]!=id){size_t size=2+p[at+1];memcpy(out+pos,p+at,size);pos+=size;}
  if(id==37){uint8_t ie[]={37,3,1,destination,1};memcpy(out+pos,ie,sizeof(ie));}
  if(id==40){uint8_t ie[]={40,6,1,1,0xff,0x7f,0,0};memcpy(out+pos,ie,sizeof(ie));}return needed;
}
}
